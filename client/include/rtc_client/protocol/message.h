#pragma once
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <string_view>

namespace rtc_client::protocol {
using Json = nlohmann::json;
inline constexpr std::size_t max_message_bytes = 256 * 1024;
enum class Kind { request, response, notification };
struct Message {
  Kind kind;
  std::string id, method;
  bool ok{false};
  Json data = Json::object();
  int error_code{};
};
class InvalidMessage final : public std::runtime_error {
 public:
  InvalidMessage() : std::runtime_error("Invalid talk.v1 envelope") {}
};
bool valid_method(std::string_view) noexcept;
Message decode(std::string_view);
std::string request(std::string id, std::string method, const Json& data);
std::string success(std::string id, const Json& data);
std::string failure(std::string id, int code);
std::string notification(std::string method, const Json& data);
}  // namespace rtc_client::protocol
