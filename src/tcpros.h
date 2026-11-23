// TCPROS connection header and length-prefixed framing.
#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace rosc {

using Fields = std::map<std::string, std::string>;

constexpr uint32_t kMaxHeader = 1u << 20;
constexpr uint32_t kMaxMessage = 1u << 30;

// Full header including its 4-byte length prefix.
std::string tcpros_header(const std::vector<std::pair<std::string, std::string>>& fields);
// [len][data] with the length prefix.
std::shared_ptr<const std::string> tcpros_frame(const void* data, size_t n);

enum class Take { NeedMore, Ok, Bad };
// Takes one [len][payload] block starting at buf[*pos]; advances *pos on Ok.
Take tcpros_take(const std::string& buf, size_t* pos, uint32_t max, std::string* payload);
bool tcpros_parse_fields(const std::string& payload, Fields* out);

}  // namespace rosc
