#pragma once
#include "rtc_client/protocol/message.h"
#include <string>
#include <string_view>

namespace rtc_client::logging {
enum class Level { debug, info, warning, error };
// Application owns the logging lifetime; all SDK/render workers must stop before
// the last Runtime is destroyed. Never invoke from realtime audio callbacks.
class Runtime final {
 public:
  explicit Runtime(const std::string& file, bool debug = false) noexcept;
  ~Runtime();
  Runtime(const Runtime&) = delete;
  Runtime& operator=(const Runtime&) = delete;
  bool healthy() const noexcept;
};
protocol::Json allowed_fields(const protocol::Json&);
void emit(Level, std::string_view event, const protocol::Json& fields = protocol::Json::object()) noexcept;
}  // namespace rtc_client::logging
