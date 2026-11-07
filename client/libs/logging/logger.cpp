#include "rtc_client/logging/logger.h"
#include <rtc_log_adapter.h>
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <mutex>
#include <set>

namespace rtc_client::logging {
namespace {
std::mutex mutex;
unsigned users{};
bool initialized{};
uint32_t convert(Level level) {
  switch (level) {
    case Level::debug: return RTC_LOG_DEBUG;
    case Level::warning: return RTC_LOG_WARN;
    case Level::error: return RTC_LOG_ERROR;
    default: return RTC_LOG_INFO;
  }
}
bool label(std::string_view text) {
  if (text.empty() || text.size() > 128) return false;
  for (unsigned char c : text) {
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
          (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-')) return false;
  }
  // String fields are fixed enum/method labels, NOT arbitrary exception text.
  const std::string value(text);
  if (std::count(value.begin(), value.end(), '.') >= 2) return false;
  return true;
}
}  // namespace

protocol::Json allowed_fields(const protocol::Json& fields) {
  auto result = protocol::Json::object();
  if (!fields.is_object()) return result;
  static const std::set<std::string> numeric = {
    "generation", "operation", "sequence", "code", "http_status", "close_code",
    "elapsed_ms", "pending", "bytes", "retry", "count", "dropped", "ok"
  };
  static const std::set<std::string> labels = {"method", "state", "pc", "reason"};
  for (auto it = fields.begin(); it != fields.end(); ++it) {
    if (numeric.count(it.key()) && (it.value().is_number() || it.value().is_boolean()))
      result[it.key()] = it.value();
    else if (labels.count(it.key()) && it.value().is_string()) {
      auto text = it.value().get<std::string>();
      // Defense in depth: labels must not be credentials, URLs, SDP or JWTs.
      if (label(text)) result[it.key()] = std::move(text);
    }
  }
  return result;
}

Runtime::Runtime(const std::string& file, bool debug) noexcept {
  std::lock_guard<std::mutex> lock(mutex);
  if (users++ != 0) return;
  try {
    if (file.empty()) throw std::runtime_error("missing log path");
    const auto parent = std::filesystem::path(file).parent_path();
    if (!parent.empty()) std::filesystem::create_directories(parent);
    rtc_log_options config{};
    config.struct_size = sizeof(config);
    config.abi_version = RTC_LOG_ABI_VERSION;
    config.file_path = file.c_str();
    config.max_file_bytes = 2 * 1024 * 1024;
    config.rotated_files = 4;
    config.level = debug ? RTC_LOG_DEBUG : RTC_LOG_INFO;
    config.queue_size = 2048;
    initialized = rtc_log_init(&config) == RTC_LOG_OK;
  } catch (...) { initialized = false; }
  if (!initialized) std::fputs("rtc-rk: file logging unavailable\n", stderr);
}
Runtime::~Runtime() {
  std::lock_guard<std::mutex> lock(mutex);
  if (--users == 0 && initialized) {
    rtc_log_shutdown();
    initialized = false;
  }
}
bool Runtime::healthy() const noexcept {
  std::lock_guard<std::mutex> lock(mutex);
  return initialized;
}
void emit(Level level, std::string_view event, const protocol::Json& fields) noexcept {
  try {
    if (!label(event)) return;
    const auto line = protocol::Json({{"event",event},{"fields",allowed_fields(fields)}}).dump();
    std::lock_guard<std::mutex> lock(mutex);
    // The formatter may be statically linked into isolated media DSOs. Runtime
    // ownership stays in the composition root; the shared C SDK owns the one
    // process-wide readiness check and safely rejects writes before/after it.
    (void)rtc_log_write(convert(level),line.data(),line.size());
  } catch (...) {}
}
}  // namespace rtc_client::logging
