#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>

#include "net.h"
#include "node_impl.h"
#include "xmlrpc.h"

namespace rosc {

namespace {

constexpr size_t kMaxHttp = 1 << 20;

void fail(Conn& c, const std::string& why) {
  if (c.kind == Conn::SubLink || c.kind == Conn::HttpClient)
    fprintf(stderr, "rosc: %s from %s: %s\n", c.sub->topic.c_str(), c.pub_uri.c_str(), why.c_str());
  c.dead = true;
}

void reply_error(Conn& c, const std::string& msg) {
  c.out = tcpros_header({{"error", msg}});
  c.close_after_write = true;
}

XValue ok(XValue v) { return XValue(std::vector<XValue>{1, "", std::move(v)}); }

std::vector<XValue> topic_list(const std::vector<std::pair<std::string, std::string>>& l) {
  std::vector<XValue> r;
  for (auto& t : l) r.push_back(std::vector<XValue>{t.first, t.second});
  return r;
}

XValue handle_api(rosc_node* n, const std::string& m, const std::vector<XValue>& p) {
  auto str = [&](size_t i) { return i < p.size() ? p[i].s : std::string(); };
  if (m == "getPid") return ok((int)getpid());
  if (m == "getMasterUri") return ok(n->master_uri);
  if (m == "getBusInfo") return ok(std::vector<XValue>{});
  if (m == "getBusStats") return ok(std::vector<XValue>{std::vector<XValue>{}, std::vector<XValue>{}, std::vector<XValue>{}});
  if (m == "paramUpdate") return ok(0);
  if (m == "shutdown") {
    n->shut = true;
    n->cv.notify_all();
    return ok(0);
  }
  if (m == "getPublications" || m == "getSubscriptions") {
    std::vector<std::pair<std::string, std::string>> l;
    if (m == "getPublications")
      for (auto& pb : n->pubs) l.push_back({pb->topic, pb->type});
    else
      for (auto& s : n->subs) l.push_back({s->topic, s->type});
    return ok(topic_list(l));
  }
  if (m == "publisherUpdate") {
    std::vector<std::string> uris;
    if (p.size() > 2)
      for (auto& u : p[2].a) uris.push_back(u.s);
    for (auto& s : std::vector<std::shared_ptr<rosc_sub>>(n->subs))
      if (s->topic == str(1)) update_publishers(n, s, uris);
    return ok(0);
  }
  if (m == "requestTopic") {
    bool tcpros = false;
    if (p.size() > 2)
      for (auto& proto : p[2].a)
        if (!proto.a.empty() && proto.a[0].s == "TCPROS") tcpros = true;
    bool have = std::any_of(n->pubs.begin(), n->pubs.end(), [&](auto& pb) { return pb->topic == str(1); });
    if (!have) return XValue(std::vector<XValue>{-1, "not a publisher of " + str(1), 0});
    if (!tcpros) return XValue(std::vector<XValue>{0, "only TCPROS is supported", 0});
    return ok(std::vector<XValue>{"TCPROS", n->host, n->tcp_port});
  }
  return XValue(std::vector<XValue>{-1, "unknown method: " + m, 0});
}

// Our XML-RPC server: one request per connection.
void on_http_server(rosc_node* n, Conn& c) {
  std::string body, method;
  std::vector<XValue> params;
  if (c.in.size() > kMaxHttp) return void(c.dead = true);
  if (!http_split(c.in, c.eof, &body)) return;
  c.in.clear();
  XValue r = xml_parse_call(body, &method, &params) ? handle_api(n, method, params)
                                                    : XValue(std::vector<XValue>{-1, "malformed request", 0});
  c.out = http_response(xml_response(r));
  c.close_after_write = true;
}

// requestTopic response from a publisher: connect to its TCPROS port.
void on_http_client(rosc_node* n, Conn& c) {
  std::string body, err;
  XValue r;
  if (c.in.size() > kMaxHttp) return fail(c, "response too large");
  if (!http_split(c.in, c.eof, &body)) return;
  c.dead = true;
  if (!xml_parse_response(body, &r, &err)) return fail(c, err);
  if (r.a.size() < 3 || r.a[0].i != 1 || r.a[2].a.size() < 3 || r.a[2].a[0].s != "TCPROS")
    return fail(c, "requestTopic refused: " + (r.a.size() > 1 ? r.a[1].s : std::string("?")));
  int fd = tcp_connect(r.a[2].a[1].s, (int)r.a[2].a[2].i, &err);
  if (fd < 0) return fail(c, err);
  auto l = std::make_unique<Conn>(Conn::SubLink, fd);
  l->connecting = true;
  l->sub = c.sub;
  l->pub_uri = c.pub_uri;
  l->out = tcpros_header({{"callerid", n->name},
                          {"topic", c.sub->topic},
                          {"md5sum", c.sub->md5sum},
                          {"type", c.sub->type},
                          {"tcp_nodelay", "1"}});
  n->conns.push_back(std::move(l));
}

// Header from a connecting subscriber.
void on_accepted(rosc_node* n, Conn& c) {
  size_t pos = 0;
  std::string payload;
  Fields f;
  Take t = tcpros_take(c.in, &pos, kMaxHeader, &payload);
  if (t == Take::NeedMore) return;
  c.in.clear();
  if (t == Take::Bad || !tcpros_parse_fields(payload, &f)) return reply_error(c, "malformed header");
  if (!f.count("topic")) return reply_error(c, "only topics are supported");
  auto it = std::find_if(n->pubs.begin(), n->pubs.end(), [&](auto& pb) { return pb->topic == f["topic"]; });
  if (it == n->pubs.end()) return reply_error(c, "not a publisher of " + f["topic"]);
  rosc_pub* pb = it->get();
  const std::string& md5 = f["md5sum"];
  if (md5 != "*" && pb->md5sum != "*" && md5 != pb->md5sum)
    return reply_error(c, "md5sum mismatch: " + md5 + " != " + pb->md5sum + " (" + pb->type + ")");
  c.out = tcpros_header({{"callerid", n->name},
                         {"topic", pb->topic},
                         {"md5sum", pb->md5sum},
                         {"type", pb->type},
                         {"message_definition", pb->msg_def},
                         {"latching", pb->latch ? "1" : "0"}});
  if (f["tcp_nodelay"] == "1") set_nodelay(c.fd);
  c.kind = Conn::PubLink;
  c.pub = pb;
  if (pb->latch && pb->last) c.q.push_back(pb->last);
}

void on_sub_link(rosc_node* n, Conn& c) {
  size_t pos = 0;
  std::string payload;
  rosc_sub* s = c.sub.get();
  if (!c.got_header) {
    Fields f;
    Take t = tcpros_take(c.in, &pos, kMaxHeader, &payload);
    if (t == Take::NeedMore) return;
    if (t == Take::Bad || !tcpros_parse_fields(payload, &f)) return fail(c, "malformed header");
    if (f.count("error")) return fail(c, f["error"]);
    if (s->md5sum != "*" && f["md5sum"] != "*" && f["md5sum"] != s->md5sum)
      return fail(c, "md5sum mismatch: " + f["md5sum"] + " != " + s->md5sum);
    c.info = std::make_shared<LinkInfo>();
    LinkInfo& li = *c.info;
    li.topic = s->topic;
    li.type = f["type"];
    li.md5sum = f["md5sum"];
    li.msg_def = f["message_definition"];
    li.callerid = f["callerid"];
    li.c = {li.topic.c_str(), li.type.c_str(), li.md5sum.c_str(), li.msg_def.c_str(), li.callerid.c_str(),
            f["latching"] == "1"};
    c.got_header = true;
  }
  for (;;) {
    Take t = tcpros_take(c.in, &pos, kMaxMessage, &payload);
    if (t == Take::NeedMore) break;
    if (t == Take::Bad) return fail(c, "message too large");
    if (!s->active) continue;
    s->q.push_back(Pending{c.sub, std::move(payload), c.info});
    if (s->queue_size > 0 && s->q.size() > (size_t)s->queue_size) s->q.pop_front();
    n->cv.notify_all();
  }
  c.in.erase(0, pos);
}

void on_readable(rosc_node* n, Conn& c) {
  char buf[65536];
  for (;;) {
    ssize_t r = recv(c.fd, buf, sizeof(buf), 0);
    if (r > 0) c.in.append(buf, r);
    else if (r == 0) c.eof = true;
    else if (errno == EINTR) continue;
    else if (errno != EAGAIN && errno != EWOULDBLOCK) c.eof = true;
    if (r <= 0) break;
  }
  switch (c.kind) {
    case Conn::HttpServer: on_http_server(n, c); break;
    case Conn::HttpClient: on_http_client(n, c); break;
    case Conn::Accepted: on_accepted(n, c); break;
    case Conn::SubLink: on_sub_link(n, c); break;
    case Conn::PubLink: c.in.clear(); break;  // subscribers send nothing after the header
  }
  if (c.eof && !c.dead) {
    if (c.kind == Conn::SubLink) fail(c, "publisher closed the connection");
    else if (c.kind == Conn::HttpClient) fail(c, "no response to requestTopic");
    else if (c.kind != Conn::HttpServer || c.out.empty()) c.dead = true;
  }
}

bool wants_write(const Conn& c) { return c.connecting || c.out_off < c.out.size() || !c.q.empty(); }

void flush(Conn& c) {
  for (;;) {
    const std::string* buf;
    size_t* off;
    if (c.out_off < c.out.size()) {
      buf = &c.out;
      off = &c.out_off;
    } else if (!c.q.empty()) {
      buf = c.q.front().get();
      off = &c.q_off;
    } else {
      break;
    }
    ssize_t r = send_some(c.fd, buf->data() + *off, buf->size() - *off);
    if (r < 0) {
      if (errno == EINTR) continue;
      if (errno != EAGAIN && errno != EWOULDBLOCK) c.dead = true;
      return;
    }
    *off += r;
    if (*off < buf->size()) continue;
    if (buf == &c.out) {
      c.out.clear();
      c.out_off = 0;
    } else {
      c.q.pop_front();
      c.q_off = 0;
    }
  }
  if (c.close_after_write) c.dead = true;
}

void accept_all(rosc_node* n, int lfd, Conn::Kind kind) {
  for (;;) {
    int fd = accept(lfd, nullptr, nullptr);
    if (fd < 0) return;
    setup_socket(fd);
    n->conns.push_back(std::make_unique<Conn>(kind, fd));
  }
}

}  // namespace

void update_publishers(rosc_node* n, const std::shared_ptr<rosc_sub>& sub, const std::vector<std::string>& uris) {
  std::set<std::string> want(uris.begin(), uris.end());
  for (auto& c : n->conns)
    if (c->sub == sub && !want.count(c->pub_uri)) c->dead = true;
  for (auto it = sub->pub_uris.begin(); it != sub->pub_uris.end();)
    it = want.count(*it) ? std::next(it) : sub->pub_uris.erase(it);
  for (auto& uri : want) {
    if (sub->pub_uris.count(uri)) continue;
    std::string host, path, err;
    int port;
    int fd = parse_uri(uri, &host, &port, &path) ? tcp_connect(host, port, &err) : -1;
    if (fd < 0) {
      fprintf(stderr, "rosc: %s from %s: %s\n", sub->topic.c_str(), uri.c_str(), err.c_str());
      continue;
    }
    auto c = std::make_unique<Conn>(Conn::HttpClient, fd);
    c->connecting = true;
    c->sub = sub;
    c->pub_uri = uri;
    c->out = http_request(host, port, path,
                          xml_call("requestTopic", {n->name, sub->topic, std::vector<XValue>{XValue(std::vector<XValue>{"TCPROS"})}}));
    sub->pub_uris.insert(uri);
    n->conns.push_back(std::move(c));
  }
  n->wake_io();
}

void io_run(rosc_node* n) {
  std::vector<pollfd> pfds;
  std::vector<Conn*> pc;
  std::unique_lock<std::mutex> lk(n->mu);
  while (!n->stop) {
    pfds.assign({{n->wake[0], POLLIN, 0}, {n->xml_fd, POLLIN, 0}, {n->tcp_fd, POLLIN, 0}});
    pc.clear();
    for (auto& c : n->conns) {
      pfds.push_back({c->fd, (short)(POLLIN | (wants_write(*c) ? POLLOUT : 0)), 0});
      pc.push_back(c.get());
    }
    lk.unlock();
    int r = poll(pfds.data(), pfds.size(), -1);
    lk.lock();
    if (r < 0) continue;  // EINTR
    if (pfds[0].revents) {
      char b[64];
      while (read(n->wake[0], b, sizeof(b)) > 0) {
      }
      n->wake_pending = false;
    }
    if (pfds[1].revents) accept_all(n, n->xml_fd, Conn::HttpServer);
    if (pfds[2].revents) accept_all(n, n->tcp_fd, Conn::Accepted);
    for (size_t i = 0; i < pc.size(); i++) {
      Conn& c = *pc[i];
      short re = pfds[3 + i].revents;
      if (c.dead || !re) continue;
      if (c.connecting) {
        if (int e = socket_error(c.fd)) {
          fail(c, std::string("connect: ") + strerror(e));
          continue;
        }
        if (!(re & POLLOUT)) continue;
        c.connecting = false;
      }
      if (re & (POLLIN | POLLHUP | POLLERR)) on_readable(n, c);
      if (!c.dead && (re & POLLOUT)) flush(c);
    }
    // A dead link frees its publisher URI so the next publisherUpdate reconnects.
    for (auto& c : n->conns) {
      if (!c->dead || !c->sub) continue;
      bool other = std::any_of(n->conns.begin(), n->conns.end(), [&](auto& o) {
        return !o->dead && o->sub == c->sub && o->pub_uri == c->pub_uri;
      });
      if (!other) c->sub->pub_uris.erase(c->pub_uri);
    }
    n->conns.erase(std::remove_if(n->conns.begin(), n->conns.end(),
                                  [](auto& c) {
                                    if (c->dead) ::close(c->fd);
                                    return c->dead;
                                  }),
                   n->conns.end());
  }
}

}  // namespace rosc
