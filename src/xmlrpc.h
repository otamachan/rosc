// Minimal XML-RPC over HTTP: just what the ROS1 master/slave APIs use.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace rosc {

struct XValue {
  enum Type { Nil, Int, Bool, Double, Str, Array, Struct } type = Nil;
  int64_t i = 0;
  double d = 0;
  std::string s;
  std::vector<XValue> a;           // Array elements / Struct member values
  std::vector<std::string> names;  // Struct member names

  XValue() = default;
  XValue(int v) : type(Int), i(v) {}
  XValue(const char* v) : type(Str), s(v) {}
  XValue(std::string v) : type(Str), s(std::move(v)) {}
  XValue(std::vector<XValue> v) : type(Array), a(std::move(v)) {}
};

std::string xml_call(const std::string& method, const std::vector<XValue>& params);
std::string xml_response(const XValue& v);
bool xml_parse_call(const std::string& body, std::string* method, std::vector<XValue>* params);
// A fault response returns false with its faultString in *err.
bool xml_parse_response(const std::string& body, XValue* v, std::string* err);

std::string http_request(const std::string& host, int port, const std::string& path, const std::string& body);
std::string http_response(const std::string& body);
// True once buf holds a complete HTTP message; eof marks a closed peer (body without Content-Length).
bool http_split(const std::string& buf, bool eof, std::string* body);

bool parse_uri(const std::string& uri, std::string* host, int* port, std::string* path);

// Blocking call on the caller's thread.
bool xmlrpc_call(const std::string& uri, const std::string& method, const std::vector<XValue>& params,
                 int timeout_ms, XValue* result, std::string* err);

}  // namespace rosc
