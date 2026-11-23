// Unit tests for the protocol pieces; run by ctest.
#undef NDEBUG
#include <cassert>
#include <cstdio>
#include <cstring>

#include "node_impl.h"
#include "rosc/msgs.h"
#include "tcpros.h"
#include "xmlrpc.h"

using namespace rosc;

static void test_xmlrpc() {
  std::string method;
  std::vector<XValue> params;
  std::string call = xml_call("publisherUpdate", {"/master", "/a<b>&c", std::vector<XValue>{"http://h:1/", 7}});
  assert(xml_parse_call(call, &method, &params));
  assert(method == "publisherUpdate" && params.size() == 3);
  assert(params[1].s == "/a<b>&c");
  assert(params[2].a.size() == 2 && params[2].a[0].s == "http://h:1/" && params[2].a[1].i == 7);

  // As written by Python's xmlrpc (rosmaster).
  const char* py =
      "<?xml version='1.0'?>\n<methodResponse>\n<params>\n<param>\n<value><array><data>\n"
      "<value><int>1</int></value>\n<value><string>Subscribed to [/t]</string></value>\n"
      "<value><array><data>\n<value><string>http://h:1/</string></value>\n</data></array></value>\n"
      "</data></array></value>\n</param>\n</params>\n</methodResponse>\n";
  XValue v;
  std::string err;
  assert(xml_parse_response(py, &v, &err));
  assert(v.type == XValue::Array && v.a.size() == 3 && v.a[0].i == 1 && v.a[2].a[0].s == "http://h:1/");

  const char* misc =
      "<methodResponse><params><param><value><array><data><value>untyped &amp; &#65;</value>"
      "<value><array><data>\n</data></array></value><value><array><data/></array></value>"
      "<value><boolean>1</boolean></value><value><i4>-3</i4></value><value><double>1.5</double></value>"
      "<value/><value><string/></value></data></array></value></param></params></methodResponse>";
  assert(xml_parse_response(misc, &v, &err));
  assert(v.a.size() == 8 && v.a[0].s == "untyped & A" && v.a[1].a.empty() && v.a[2].a.empty());
  assert(v.a[3].type == XValue::Bool && v.a[3].i == 1 && v.a[4].i == -3 && v.a[5].d == 1.5 && v.a[6].s.empty());

  const char* fault =
      "<methodResponse><fault><value><struct><member><name>faultCode</name><value><int>1</int></value></member>"
      "<member><name>faultString</name><value><string>boom</string></value></member></struct></value></fault>"
      "</methodResponse>";
  assert(!xml_parse_response(fault, &v, &err) && err.find("boom") != std::string::npos);
  assert(!xml_parse_response("<methodResponse><params>", &v, &err));
}

static void test_http_uri() {
  std::string body, host, path;
  int port;
  assert(!http_split("HTTP/1.0 200 OK\r\nContent-length: 5\r\n\r\nabc", false, &body));
  assert(http_split("HTTP/1.0 200 OK\r\nContent-length: 5\r\n\r\nabcde", false, &body) && body == "abcde");
  assert(!http_split("HTTP/1.0 200 OK\r\n\r\nxy", false, &body));
  assert(http_split("HTTP/1.0 200 OK\r\n\r\nxy", true, &body) && body == "xy");
  assert(parse_uri("http://host:11311/", &host, &port, &path) && host == "host" && port == 11311 && path == "/");
  assert(parse_uri("http://10.0.0.1:80", &host, &port, &path) && path == "/");
  assert(!parse_uri("http://host/", &host, &port, &path));
  assert(!parse_uri("https://host:1/", &host, &port, &path));
}

static void test_names() {
  assert(resolve_name("/", "/node", "chatter") == "/chatter");
  assert(resolve_name("/ns", "/ns/node", "chatter") == "/ns/chatter");
  assert(resolve_name("/ns", "/ns/node", "/abs") == "/abs");
  assert(resolve_name("/ns", "/ns/node", "~priv") == "/ns/node/priv");
  assert(resolve_name("/ns", "/ns/node", "~/priv") == "/ns/node/priv");
  assert(resolve_name("/", "", "ns/") == "/ns");
  assert(resolve_name("/", "", "").empty());
}

static void test_tcpros() {
  std::string h = tcpros_header({{"topic", "/t"}, {"md5sum", "*"}, {"message_definition", "a=b"}});
  size_t pos = 0;
  std::string payload;
  assert(tcpros_take(h.substr(0, 5), &pos, kMaxHeader, &payload) == Take::NeedMore);
  assert(tcpros_take(h, &pos, kMaxHeader, &payload) == Take::Ok && pos == h.size());
  Fields f;
  assert(tcpros_parse_fields(payload, &f) && f["topic"] == "/t" && f["message_definition"] == "a=b");
  auto fr = tcpros_frame("xyz", 3);
  pos = 0;
  assert(tcpros_take(*fr, &pos, 2, &payload) == Take::Bad);
  assert(tcpros_take(*fr, &pos, 10, &payload) == Take::Ok && payload == "xyz");
}

static void test_msgs() {
  uint8_t buf[256];
  size_t n = rosc_string_serialize("hi", 2, buf, sizeof(buf));
  const char* s;
  size_t len;
  assert(n == 6 && rosc_string_parse(buf, n, &s, &len) && len == 2 && memcmp(s, "hi", 2) == 0);
  assert(!rosc_string_parse(buf, n - 1, &s, &len));
  assert(rosc_string_serialize("hi", 2, buf, 3) == 6);

  rosc_twist_t t = {{1, 2, 3}, {4, 5, 6}}, t2;
  n = rosc_twist_serialize(&t, buf, sizeof(buf));
  assert(n == 48 && rosc_twist_parse(buf, n, &t2) && t2.linear[1] == 2 && t2.angular[2] == 6);

  float r[3] = {1.f, 2.f, 3.f};
  rosc_laser_scan_t sc{}, sc2;
  sc.seq = 9;
  sc.frame_id = "laser";
  sc.frame_id_len = 5;
  sc.angle_max = 1.5f;
  sc.ranges = r;
  sc.ranges_len = 3;
  n = rosc_laser_scan_serialize(&sc, buf, sizeof(buf));
  assert(n == 12 + 4 + 5 + 28 + 4 + 12 + 4);
  assert(rosc_laser_scan_parse(buf, n, &sc2));
  assert(sc2.seq == 9 && sc2.frame_id_len == 5 && sc2.angle_max == 1.5f && sc2.ranges_len == 3);
  assert(rosc_float_at(sc2.ranges, 2) == 3.f && sc2.intensities_len == 0);
  buf[n - 4] = 0xff;  // intensities length larger than the buffer
  assert(!rosc_laser_scan_parse(buf, n, &sc2));
}

int main() {
  test_xmlrpc();
  test_http_uri();
  test_names();
  test_tcpros();
  test_msgs();
  printf("ok\n");
  return 0;
}
