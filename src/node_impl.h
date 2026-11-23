// Internal node state shared by node.cpp (API, master) and io_loop.cpp (I/O thread).
#pragma once

#include <atomic>
#include <condition_variable>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "rosc/rosc.h"
#include "tcpros.h"

namespace rosc {

struct LinkInfo {
  std::string topic, type, md5sum, msg_def, callerid;
  rosc_msg_info_t c{};
};

struct Pending {
  std::shared_ptr<rosc_sub> sub;
  std::string data;
  std::shared_ptr<const LinkInfo> info;
};

struct Conn {
  enum Kind { HttpServer, HttpClient, Accepted, PubLink, SubLink } kind;
  int fd;
  bool connecting = false, dead = false, eof = false, close_after_write = false, got_header = false;
  std::string in, out;  // out: headers / HTTP, sent before q
  size_t out_off = 0;
  std::deque<std::shared_ptr<const std::string>> q;  // framed messages (PubLink)
  size_t q_off = 0;
  rosc_pub* pub = nullptr;               // PubLink
  std::shared_ptr<rosc_sub> sub;         // HttpClient, SubLink
  std::string pub_uri;                   // HttpClient, SubLink
  std::shared_ptr<LinkInfo> info;        // SubLink

  Conn(Kind k, int f) : kind(k), fd(f) {}
};

}  // namespace rosc

struct rosc_pub {
  rosc_node* node;
  std::string topic, type, md5sum, msg_def;
  int queue_size;
  bool latch;
  std::shared_ptr<const std::string> last;  // latched frame
};

struct rosc_sub {
  rosc_node* node;
  std::string topic, type, md5sum;
  int queue_size;
  rosc_sub_cb cb;
  void* user;
  std::atomic<bool> active{true};
  std::deque<rosc::Pending> q;
  std::set<std::string> pub_uris;  // publishers we are (or are getting) connected to
};

struct rosc_node {
  std::string name, ns, master_uri, host, caller_api;
  std::map<std::string, std::string> remaps;
  int master_timeout_ms = 5000;
  int xml_fd = -1, tcp_fd = -1, tcp_port = 0, wake[2] = {-1, -1};

  // ponytail: one lock for all node state; split per connection if publish contention shows up.
  std::mutex mu;
  std::condition_variable cv;      // spin_once wakeup
  std::recursive_mutex cb_mu;      // held while callbacks run
  std::atomic<bool> shut{false}, stop{false}, wake_pending{false};
  std::vector<std::unique_ptr<rosc_pub>> pubs;
  std::vector<std::shared_ptr<rosc_sub>> subs;
  std::vector<std::unique_ptr<rosc::Conn>> conns;
  std::thread io;

  void wake_io();
};

namespace rosc {
void io_run(rosc_node* n);
// Resolves name against ns ("/a/b") and the node's full name (for "~"); no remapping.
std::string resolve_name(const std::string& ns, const std::string& node, const std::string& name);
// Reconciles a subscription's publisher connections with the master's list. Requires n->mu.
void update_publishers(rosc_node* n, const std::shared_ptr<rosc_sub>& sub, const std::vector<std::string>& uris);
}  // namespace rosc
