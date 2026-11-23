#include "tcpros.h"

namespace rosc {

static void put_u32(std::string& s, uint32_t v) {
  for (int i = 0; i < 4; i++) s += (char)((v >> (8 * i)) & 0xff);
}

static uint32_t get_u32(const char* p) {
  uint32_t v = 0;
  for (int i = 0; i < 4; i++) v |= (uint32_t)(uint8_t)p[i] << (8 * i);
  return v;
}

std::string tcpros_header(const std::vector<std::pair<std::string, std::string>>& fields) {
  std::string body;
  for (auto& f : fields) {
    put_u32(body, (uint32_t)(f.first.size() + 1 + f.second.size()));
    body += f.first + "=" + f.second;
  }
  std::string out;
  put_u32(out, (uint32_t)body.size());
  return out + body;
}

std::shared_ptr<const std::string> tcpros_frame(const void* data, size_t n) {
  auto s = std::make_shared<std::string>();
  s->reserve(4 + n);
  put_u32(*s, (uint32_t)n);
  s->append((const char*)data, n);
  return s;
}

Take tcpros_take(const std::string& buf, size_t* pos, uint32_t max, std::string* payload) {
  if (buf.size() - *pos < 4) return Take::NeedMore;
  uint32_t len = get_u32(buf.data() + *pos);
  if (len > max) return Take::Bad;
  if (buf.size() - *pos - 4 < len) return Take::NeedMore;
  payload->assign(buf, *pos + 4, len);
  *pos += 4 + len;
  return Take::Ok;
}

bool tcpros_parse_fields(const std::string& payload, Fields* out) {
  size_t p = 0;
  while (p < payload.size()) {
    if (payload.size() - p < 4) return false;
    uint32_t len = get_u32(payload.data() + p);
    p += 4;
    if (payload.size() - p < len) return false;
    std::string f = payload.substr(p, len);
    p += len;
    size_t eq = f.find('=');
    if (eq == std::string::npos) return false;
    (*out)[f.substr(0, eq)] = f.substr(eq + 1);
  }
  return true;
}

}  // namespace rosc
