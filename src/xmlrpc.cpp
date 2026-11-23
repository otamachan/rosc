#include "xmlrpc.h"

#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cctype>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "net.h"

namespace rosc {

namespace {

std::string escape(const std::string& in) {
  std::string o;
  for (char c : in) {
    switch (c) {
      case '<': o += "&lt;"; break;
      case '>': o += "&gt;"; break;
      case '&': o += "&amp;"; break;
      default: o += c;
    }
  }
  return o;
}

std::string unescape(const std::string& in) {
  std::string o;
  for (size_t i = 0; i < in.size(); i++) {
    if (in[i] != '&') {
      o += in[i];
      continue;
    }
    size_t e = in.find(';', i);
    if (e == std::string::npos) {
      o += in[i];
      continue;
    }
    std::string ent = in.substr(i + 1, e - i - 1);
    if (ent == "lt") o += '<';
    else if (ent == "gt") o += '>';
    else if (ent == "amp") o += '&';
    else if (ent == "quot") o += '"';
    else if (ent == "apos") o += '\'';
    else if (!ent.empty() && ent[0] == '#') {
      long cp = ent.size() > 1 && ent[1] == 'x' ? strtol(ent.c_str() + 2, nullptr, 16) : strtol(ent.c_str() + 1, nullptr, 10);
      if (cp < 0x80) o += (char)cp;
      else if (cp < 0x800) o += (char)(0xC0 | (cp >> 6)), o += (char)(0x80 | (cp & 0x3F));
      else if (cp < 0x10000) o += (char)(0xE0 | (cp >> 12)), o += (char)(0x80 | ((cp >> 6) & 0x3F)), o += (char)(0x80 | (cp & 0x3F));
      else o += (char)(0xF0 | (cp >> 18)), o += (char)(0x80 | ((cp >> 12) & 0x3F)), o += (char)(0x80 | ((cp >> 6) & 0x3F)), o += (char)(0x80 | (cp & 0x3F));
    } else {
      o += in.substr(i, e - i + 1);
    }
    i = e;
  }
  return o;
}

void emit(const XValue& v, std::string& o) {
  o += "<value>";
  switch (v.type) {
    case XValue::Int: o += "<int>" + std::to_string(v.i) + "</int>"; break;
    case XValue::Bool: o += v.i ? "<boolean>1</boolean>" : "<boolean>0</boolean>"; break;
    case XValue::Double: {
      char b[64];
      snprintf(b, sizeof(b), "%.17g", v.d);
      o += std::string("<double>") + b + "</double>";
      break;
    }
    case XValue::Nil:  // Python's xmlrpc rejects <nil/> unless allow_none; send "" instead
    case XValue::Str: o += "<string>" + escape(v.s) + "</string>"; break;
    case XValue::Array:
      o += "<array><data>";
      for (auto& e : v.a) emit(e, o);
      o += "</data></array>";
      break;
    case XValue::Struct:
      o += "<struct>";
      for (size_t k = 0; k < v.a.size(); k++) {
        o += "<member><name>" + escape(v.names[k]) + "</name>";
        emit(v.a[k], o);
        o += "</member>";
      }
      o += "</struct>";
      break;
  }
  o += "</value>";
}

struct Parser {
  const std::string& s;
  size_t p = 0;

  void skip() {
    for (;;) {
      while (p < s.size() && isspace((unsigned char)s[p])) p++;
      const char* end = s.compare(p, 2, "<?") == 0 ? "?>" : s.compare(p, 4, "<!--") == 0 ? "-->" : nullptr;
      if (!end) return;
      size_t e = s.find(end, p);
      p = e == std::string::npos ? s.size() : e + strlen(end);
    }
  }
  // Reads the next tag; closing tags come back as "/name". Attributes are dropped.
  bool tag(std::string* name, bool* selfclose) {
    skip();
    if (p >= s.size() || s[p] != '<') return false;
    size_t e = s.find('>', p);
    if (e == std::string::npos) return false;
    std::string t = s.substr(p + 1, e - p - 1);
    p = e + 1;
    *selfclose = !t.empty() && t.back() == '/';
    if (*selfclose) t.pop_back();
    size_t sp = t.find_first_of(" \t\r\n");
    if (sp != std::string::npos) t.resize(sp);
    *name = t;
    return true;
  }
  std::string peek() {
    size_t save = p;
    std::string t;
    bool sc;
    if (!tag(&t, &sc)) t.clear();
    p = save;
    return t;
  }
  bool open(const char* n) {
    std::string t;
    bool sc;
    return tag(&t, &sc) && !sc && t == n;
  }
  bool close(const std::string& n) {
    std::string t;
    bool sc;
    return tag(&t, &sc) && t == "/" + n;
  }
  std::string text() {
    size_t e = s.find('<', p);
    if (e == std::string::npos) e = s.size();
    std::string r = unescape(s.substr(p, e - p));
    p = e;
    return r;
  }

  bool value(XValue* v, int depth = 0) {
    if (depth > 64) return false;
    std::string t;
    bool sc;
    if (!tag(&t, &sc) || t != "value") return false;
    if (sc) {
      *v = XValue("");
      return true;
    }
    std::string raw = text();
    if (!tag(&t, &sc)) return false;
    if (t == "/value") {  // untyped value is a string
      *v = XValue(raw);
      return true;
    }
    if (sc) {
      if (t == "nil") *v = XValue();
      else if (t == "array") *v = XValue(std::vector<XValue>{});
      else *v = XValue("");
      return close("value");
    }
    if (t == "i4" || t == "int" || t == "i8") {
      v->type = XValue::Int;
      v->i = strtoll(text().c_str(), nullptr, 10);
    } else if (t == "boolean") {
      v->type = XValue::Bool;
      v->i = strtol(text().c_str(), nullptr, 10) != 0;
    } else if (t == "double") {
      v->type = XValue::Double;
      v->d = strtod(text().c_str(), nullptr);
    } else if (t == "string") {
      *v = XValue(text());
    } else if (t == "array") {
      *v = XValue(std::vector<XValue>{});
      std::string d;
      if (!tag(&d, &sc) || d != "data") return false;
      if (!sc) {
        while (peek() == "value") {
          v->a.emplace_back();
          if (!value(&v->a.back(), depth + 1)) return false;
        }
        if (!close("data")) return false;
      }
    } else if (t == "struct") {
      v->type = XValue::Struct;
      while (peek() == "member") {
        open("member");
        if (!open("name")) return false;
        v->names.push_back(text());
        if (!close("name")) return false;
        v->a.emplace_back();
        if (!value(&v->a.back(), depth + 1) || !close("member")) return false;
      }
    } else {
      return false;
    }
    return close(t) && close("value");
  }
};

}  // namespace

std::string xml_call(const std::string& method, const std::vector<XValue>& params) {
  std::string o = "<?xml version=\"1.0\"?><methodCall><methodName>" + escape(method) + "</methodName><params>";
  for (auto& v : params) {
    o += "<param>";
    emit(v, o);
    o += "</param>";
  }
  return o + "</params></methodCall>";
}

std::string xml_response(const XValue& v) {
  std::string o = "<?xml version=\"1.0\"?><methodResponse><params><param>";
  emit(v, o);
  return o + "</param></params></methodResponse>";
}

bool xml_parse_call(const std::string& body, std::string* method, std::vector<XValue>* params) {
  Parser ps{body};
  if (!ps.open("methodCall") || !ps.open("methodName")) return false;
  *method = ps.text();
  if (!ps.close("methodName")) return false;
  params->clear();
  std::string t = ps.peek();
  if (t == "params") {
    std::string n;
    bool sc;
    ps.tag(&n, &sc);
    if (!sc) {
      while (ps.peek() == "param") {
        ps.open("param");
        params->emplace_back();
        if (!ps.value(&params->back()) || !ps.close("param")) return false;
      }
      if (!ps.close("params")) return false;
    }
  }
  return ps.close("methodCall");
}

bool xml_parse_response(const std::string& body, XValue* v, std::string* err) {
  Parser ps{body};
  if (!ps.open("methodResponse")) {
    *err = "malformed XML-RPC response";
    return false;
  }
  if (ps.peek() == "fault") {
    ps.open("fault");
    XValue f;
    *err = "XML-RPC fault";
    if (ps.value(&f) && f.type == XValue::Struct)
      for (size_t k = 0; k < f.names.size(); k++)
        if (f.names[k] == "faultString") *err += ": " + f.a[k].s;
    return false;
  }
  if (!ps.open("params") || !ps.open("param") || !ps.value(v)) {
    *err = "malformed XML-RPC response";
    return false;
  }
  return true;
}

std::string http_request(const std::string& host, int port, const std::string& path, const std::string& body) {
  return "POST " + path + " HTTP/1.1\r\nHost: " + host + ":" + std::to_string(port) +
         "\r\nUser-Agent: rosc\r\nContent-Type: text/xml\r\nConnection: close\r\nContent-Length: " +
         std::to_string(body.size()) + "\r\n\r\n" + body;
}

std::string http_response(const std::string& body) {
  return "HTTP/1.1 200 OK\r\nServer: rosc\r\nContent-Type: text/xml\r\nConnection: close\r\nContent-Length: " +
         std::to_string(body.size()) + "\r\n\r\n" + body;
}

bool http_split(const std::string& buf, bool eof, std::string* body) {
  size_t he = buf.find("\r\n\r\n");
  if (he == std::string::npos) return false;
  std::string head = buf.substr(0, he);
  for (auto& c : head) c = (char)tolower((unsigned char)c);
  size_t k = head.find("\ncontent-length:");
  if (k == std::string::npos) {
    if (!eof) return false;
    *body = buf.substr(he + 4);
    return true;
  }
  size_t len = strtoul(head.c_str() + k + 16, nullptr, 10);
  if (buf.size() - he - 4 < len) return false;
  *body = buf.substr(he + 4, len);
  return true;
}

bool parse_uri(const std::string& uri, std::string* host, int* port, std::string* path) {
  const std::string scheme = "http://";
  if (uri.compare(0, scheme.size(), scheme) != 0) return false;
  size_t hs = scheme.size();
  size_t ps = uri.find('/', hs);
  std::string hp = uri.substr(hs, ps == std::string::npos ? std::string::npos : ps - hs);
  *path = ps == std::string::npos ? "/" : uri.substr(ps);
  size_t c = hp.rfind(':');
  if (c == std::string::npos || c == 0) return false;
  *host = hp.substr(0, c);
  char* end = nullptr;
  long p = strtol(hp.c_str() + c + 1, &end, 10);
  if (*end != '\0' || p <= 0 || p > 65535) return false;
  *port = (int)p;
  return true;
}

bool xmlrpc_call(const std::string& uri, const std::string& method, const std::vector<XValue>& params,
                 int timeout_ms, XValue* result, std::string* err) {
  std::string host, path;
  int port;
  if (!parse_uri(uri, &host, &port, &path)) {
    *err = "invalid URI: " + uri;
    return false;
  }
  int fd = tcp_connect(host, port, err);
  if (fd < 0) return false;
  auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
  auto wait = [&](short ev) {
    auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
    pollfd p{fd, ev, 0};
    return left > 0 && poll(&p, 1, (int)left) > 0;
  };
  std::string out = http_request(host, port, path, xml_call(method, params)), in, body;
  bool ok = false;
  size_t off = 0;
  if (!wait(POLLOUT) || socket_error(fd) != 0) {
    *err = "cannot connect to " + uri;
    goto done;
  }
  while (off < out.size()) {
    ssize_t n = send_some(fd, out.data() + off, out.size() - off);
    if (n > 0) off += n;
    else if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) break;
    else if (!wait(POLLOUT)) break;
  }
  if (off < out.size()) {
    *err = "failed to send to " + uri;
    goto done;
  }
  for (;;) {
    char b[8192];
    ssize_t n = recv(fd, b, sizeof(b), 0);
    if (n > 0) in.append(b, n);
    if (http_split(in, n == 0, &body)) break;
    if (n == 0 || (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)) {
      *err = "connection closed by " + uri;
      goto done;
    }
    if (n < 0 && !wait(POLLIN)) {
      *err = "timeout waiting for " + uri;
      goto done;
    }
  }
  ok = xml_parse_response(body, result, err);
done:
  ::close(fd);
  return ok;
}

}  // namespace rosc
