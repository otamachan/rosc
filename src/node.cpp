#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>

#include "net.h"
#include "node_impl.h"
#include "xmlrpc.h"

using namespace rosc;

namespace {

thread_local std::string g_error;

rosc_status err(rosc_status s, const std::string& msg) {
  g_error = msg;
  return s;
}

const char* env(const char* k) {
  const char* v = getenv(k);
  return v && *v ? v : nullptr;
}

rosc_status master_call(rosc_node* n, const char* method, std::vector<XValue> params, XValue* val,
                        int timeout_ms = 0) {
  XValue r;
  std::string e;
  if (!xmlrpc_call(n->master_uri, method, params, timeout_ms ? timeout_ms : n->master_timeout_ms, &r, &e))
    return err(ROSC_ERR_MASTER, std::string(method) + ": " + e);
  if (r.a.size() < 3 || r.a[0].i != 1)
    return err(ROSC_ERR_MASTER, std::string(method) + ": " + (r.a.size() > 1 ? r.a[1].s : "malformed response"));
  if (val) *val = r.a[2];
  return ROSC_OK;
}

std::string resolve(rosc_node* n, const char* name) {
  std::string r = resolve_name(n->ns, n->name, name);
  auto it = n->remaps.find(r);
  return it == n->remaps.end() ? r : it->second;
}

}  // namespace

void rosc_node::wake_io() {
  if (!wake_pending.exchange(true)) (void)!write(wake[1], "x", 1);
}

namespace rosc {

std::string resolve_name(const std::string& ns, const std::string& node, const std::string& name) {
  std::string r;
  if (name.empty()) return r;
  if (name[0] == '/') r = name;
  else if (name[0] == '~') r = node + "/" + name.substr(1);
  else r = ns + "/" + name;
  std::string o;  // collapse "//" and drop a trailing '/'
  for (char c : r)
    if (c != '/' || o.empty() || o.back() != '/') o += c;
  if (o.size() > 1 && o.back() == '/') o.pop_back();
  if (o.empty() || o[0] != '/') o.insert(o.begin(), '/');
  return o;
}

}  // namespace rosc

extern "C" {

rosc_status rosc_node_create(const char* name, const rosc_node_options_t* opts, rosc_node_t** out) {
  if (!name || !*name || !out) return err(ROSC_ERR_INVALID_ARG, "name and out are required");
  rosc_node_options_t o{};
  if (opts) o = *opts;
  std::map<std::string, std::string> special, remaps;
  for (int i = 0; i < o.num_args && o.args; i++) {
    std::string a = o.args[i] ? o.args[i] : "";
    size_t k = a.find(":=");
    if (k == std::string::npos || k == 0) continue;
    std::string key = a.substr(0, k), val = a.substr(k + 2);
    if (key.compare(0, 2, "__") == 0) special[key] = val;
    else if (key[0] != '_') remaps[key] = val;  // "_param:=" (private params) are ignored
  }
  auto pick = [&](const char* a, const char* key, const char* e) -> std::string {
    if (a) return a;
    if (special.count(key)) return special[key];
    const char* v = e ? env(e) : nullptr;
    return v ? v : "";
  };

  auto n = std::make_unique<rosc_node>();
  n->ns = resolve_name("/", "", pick(o.ns, "__ns", "ROS_NAMESPACE").empty() ? "/" : pick(o.ns, "__ns", "ROS_NAMESPACE"));
  n->name = resolve_name(n->ns, "", special.count("__name") ? special["__name"] : name);
  n->master_uri = pick(o.master_uri, "__master", "ROS_MASTER_URI");
  n->host = pick(o.host, "__hostname", "ROS_HOSTNAME");
  if (n->host.empty()) n->host = pick(nullptr, "__ip", "ROS_IP");
  if (n->host.empty()) {
    char h[256] = {0};
    gethostname(h, sizeof(h) - 1);
    n->host = h;
  }
  if (o.master_timeout_ms > 0) n->master_timeout_ms = o.master_timeout_ms;
  for (auto& r : remaps) n->remaps[resolve_name(n->ns, n->name, r.first)] = resolve_name(n->ns, n->name, r.second);

  std::string h, p;
  int port;
  if (!parse_uri(n->master_uri, &h, &port, &p))
    return err(ROSC_ERR_INVALID_ARG, "invalid or missing master URI: '" + n->master_uri + "'");
  int xml_port;
  n->xml_fd = tcp_listen(&xml_port);
  n->tcp_fd = tcp_listen(&n->tcp_port);
  if (n->xml_fd < 0 || n->tcp_fd < 0 || make_wake_pipe(n->wake) < 0) {
    for (int fd : {n->xml_fd, n->tcp_fd, n->wake[0], n->wake[1]})
      if (fd >= 0) close(fd);
    return err(ROSC_ERR_NETWORK, std::string("cannot open sockets: ") + strerror(errno));
  }
  n->caller_api = "http://" + n->host + ":" + std::to_string(xml_port) + "/";
  rosc_node* raw = n.release();
  raw->io = std::thread(io_run, raw);
  *out = raw;
  return ROSC_OK;
}

void rosc_node_destroy(rosc_node_t* n) {
  if (!n) return;
  while (!n->subs.empty()) rosc_unsubscribe(n->subs.back().get());
  while (!n->pubs.empty()) rosc_unadvertise(n->pubs.back().get());
  rosc_node_shutdown(n);
  n->stop = true;
  n->wake_io();
  n->io.join();
  for (auto& c : n->conns) close(c->fd);
  for (int fd : {n->xml_fd, n->tcp_fd, n->wake[0], n->wake[1]}) close(fd);
  delete n;
}

void rosc_node_shutdown(rosc_node_t* n) {
  std::lock_guard<std::mutex> lk(n->mu);
  n->shut = true;
  n->cv.notify_all();
}

bool rosc_ok(const rosc_node_t* n) { return n && !n->shut; }

const char* rosc_node_name(const rosc_node_t* n) { return n->name.c_str(); }

rosc_status rosc_spin_once(rosc_node_t* n, int timeout_ms) {
  std::vector<Pending> work;
  {
    std::unique_lock<std::mutex> lk(n->mu);
    auto ready = [&] {
      return n->shut || std::any_of(n->subs.begin(), n->subs.end(), [](auto& s) { return !s->q.empty(); });
    };
    if (timeout_ms < 0) n->cv.wait(lk, ready);
    else n->cv.wait_for(lk, std::chrono::milliseconds(timeout_ms), ready);
    if (n->shut) return err(ROSC_ERR_SHUTDOWN, "node is shut down");
    for (auto& s : n->subs) {
      for (auto& p : s->q) work.push_back(std::move(p));
      s->q.clear();
    }
  }
  std::lock_guard<std::recursive_mutex> g(n->cb_mu);
  for (auto& w : work)
    if (w.sub->active) w.sub->cb(w.data.data(), w.data.size(), &w.info->c, w.sub->user);
  return ROSC_OK;
}

rosc_status rosc_advertise(rosc_node_t* n, const char* topic, const rosc_msg_type_t* t, int queue_size, bool latch,
                           rosc_pub_t** out) {
  if (!n || !topic || !*topic || !t || !t->type || !*t->type || !t->md5sum || !*t->md5sum || !out)
    return err(ROSC_ERR_INVALID_ARG, "node, topic, type (with type and md5sum) and out are required");
  if (n->shut) return err(ROSC_ERR_SHUTDOWN, "node is shut down");
  auto p = std::make_unique<rosc_pub>();
  *p = rosc_pub{n, resolve(n, topic), t->type, t->md5sum, t->definition ? t->definition : "", queue_size, latch, nullptr};
  rosc_pub* raw = p.get();
  {
    std::lock_guard<std::mutex> lk(n->mu);
    for (auto& o : n->pubs)
      if (o->topic == raw->topic) return err(ROSC_ERR_INVALID_ARG, raw->topic + " is already advertised");
    n->pubs.push_back(std::move(p));
  }
  rosc_status s = master_call(n, "registerPublisher", {n->name, raw->topic, raw->type, n->caller_api}, nullptr);
  if (s != ROSC_OK) {
    std::lock_guard<std::mutex> lk(n->mu);
    n->pubs.erase(std::find_if(n->pubs.begin(), n->pubs.end(), [&](auto& o) { return o.get() == raw; }));
    return s;
  }
  *out = raw;
  return ROSC_OK;
}

rosc_status rosc_publish(rosc_pub_t* p, const void* data, size_t size) {
  if (!p || (!data && size)) return err(ROSC_ERR_INVALID_ARG, "pub and data are required");
  if (size > kMaxMessage) return err(ROSC_ERR_INVALID_ARG, "message too large");
  rosc_node* n = p->node;
  auto f = tcpros_frame(data, size);
  std::lock_guard<std::mutex> lk(n->mu);
  if (n->shut) return err(ROSC_ERR_SHUTDOWN, "node is shut down");
  if (p->latch) p->last = f;
  bool any = false;
  for (auto& c : n->conns) {
    if (c->kind != Conn::PubLink || c->pub != p || c->dead) continue;
    c->q.push_back(f);
    size_t keep = c->q_off > 0 ? 1 : 0;  // never drop a partially sent frame
    while (p->queue_size > 0 && c->q.size() > (size_t)p->queue_size + keep) c->q.erase(c->q.begin() + keep);
    any = true;
  }
  if (any) n->wake_io();
  return ROSC_OK;
}

int rosc_pub_num_subscribers(const rosc_pub_t* p) {
  std::lock_guard<std::mutex> lk(p->node->mu);
  return (int)std::count_if(p->node->conns.begin(), p->node->conns.end(),
                            [&](auto& c) { return c->kind == Conn::PubLink && c->pub == p && !c->dead; });
}

void rosc_unadvertise(rosc_pub_t* p) {
  if (!p) return;
  rosc_node* n = p->node;
  master_call(n, "unregisterPublisher", {n->name, p->topic, n->caller_api}, nullptr, 2000);
  std::lock_guard<std::mutex> lk(n->mu);
  for (auto& c : n->conns)
    if (c->pub == p) {
      c->pub = nullptr;
      c->dead = true;
    }
  n->pubs.erase(std::find_if(n->pubs.begin(), n->pubs.end(), [&](auto& o) { return o.get() == p; }));
  n->wake_io();
}

rosc_status rosc_subscribe(rosc_node_t* n, const char* topic, const rosc_msg_type_t* t, int queue_size, rosc_sub_cb cb,
                           void* user, rosc_sub_t** out) {
  if (!n || !topic || !*topic || (t && (!t->type || !*t->type || !t->md5sum || !*t->md5sum)) || !cb || !out)
    return err(ROSC_ERR_INVALID_ARG, "node, topic, cb and out are required; type needs type and md5sum");
  if (n->shut) return err(ROSC_ERR_SHUTDOWN, "node is shut down");
  auto s = std::make_shared<rosc_sub>();
  s->node = n;
  s->topic = resolve(n, topic);
  s->type = t ? t->type : "*";
  s->md5sum = t ? t->md5sum : "*";
  s->queue_size = queue_size;
  s->cb = cb;
  s->user = user;
  {
    std::lock_guard<std::mutex> lk(n->mu);
    n->subs.push_back(s);
  }
  XValue uris;
  rosc_status st = master_call(n, "registerSubscriber", {n->name, s->topic, s->type, n->caller_api}, &uris);
  std::lock_guard<std::mutex> lk(n->mu);
  if (st != ROSC_OK) {
    n->subs.erase(std::find(n->subs.begin(), n->subs.end(), s));
    return st;
  }
  std::vector<std::string> l;
  for (auto& u : uris.a) l.push_back(u.s);
  update_publishers(n, s, l);
  *out = s.get();
  return ROSC_OK;
}

void rosc_unsubscribe(rosc_sub_t* sub) {
  if (!sub) return;
  rosc_node* n = sub->node;
  std::shared_ptr<rosc_sub> s;
  bool last = true;
  {
    std::lock_guard<std::mutex> lk(n->mu);
    auto it = std::find_if(n->subs.begin(), n->subs.end(), [&](auto& o) { return o.get() == sub; });
    if (it == n->subs.end()) return;
    s = *it;
    n->subs.erase(it);
    s->active = false;
    s->q.clear();
    for (auto& c : n->conns)
      if (c->sub == s) c->dead = true;
    for (auto& o : n->subs)
      if (o->topic == s->topic) last = false;
    n->wake_io();
  }
  // The master tracks one subscription per node and topic.
  if (last) master_call(n, "unregisterSubscriber", {n->name, s->topic, n->caller_api}, nullptr, 2000);
  std::lock_guard<std::recursive_mutex> g(n->cb_mu);  // wait for a running callback
}

const char* rosc_last_error(void) { return g_error.c_str(); }

const char* rosc_status_str(rosc_status s) {
  switch (s) {
    case ROSC_OK: return "ok";
    case ROSC_ERR_INVALID_ARG: return "invalid argument";
    case ROSC_ERR_MASTER: return "master error";
    case ROSC_ERR_NETWORK: return "network error";
    case ROSC_ERR_SHUTDOWN: return "shut down";
    case ROSC_ERR_TIMEOUT: return "timeout";
    case ROSC_ERR_NOMEM: return "out of memory";
    case ROSC_ERR_INTERNAL: return "internal error";
  }
  return "unknown";
}

}  // extern "C"
