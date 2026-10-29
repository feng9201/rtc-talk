#include "rtcwin/log_adapter/log_api.h"

#include <w_log_cpp.h>

#include <atomic>
#include <cstring>
#include <exception>
#include <mutex>
#include <string>

namespace {

std::mutex g_mutex;
std::atomic<bool> g_initialized{false};
rtcwin_log_mode_t g_mode = RTCWIN_LOG_MODE_OFF;
std::string g_last_error;

w_log_cpp::Level ToLevel(int level) {
  switch (level) {
    case RTCWIN_LOG_TRACE:
      return w_log_cpp::Level::Trace;
    case RTCWIN_LOG_DEBUG:
      return w_log_cpp::Level::Debug;
    case RTCWIN_LOG_WARN:
      return w_log_cpp::Level::Warn;
    case RTCWIN_LOG_ERROR:
      return w_log_cpp::Level::Err;
    case RTCWIN_LOG_CRITICAL:
      return w_log_cpp::Level::Critical;
    default:
      return w_log_cpp::Level::Info;
  }
}

bool TryInitialize(const w_log_cpp::InitConfig& config) {
  try {
    return w_log_cpp::init(config);
  } catch (const std::exception& error) {
    g_last_error = error.what();
  } catch (...) {
    g_last_error = "log-cpp initialization failed";
  }
  return false;
}

}  // namespace

extern "C" {

int rtcwin_log_initialize(const char* file_path,
                          int enable_console_fallback) {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (g_initialized) return 1;

  g_last_error.clear();
  w_log_cpp::InitConfig config;
  config.name = "rtcwin";
  config.enable_console = false;
  config.file_path = file_path != nullptr ? file_path : "";
  config.max_file_size = 5 * 1024 * 1024;
  config.max_file_num = 5;
  config.level = w_log_cpp::Level::Trace;
  config.file_level = w_log_cpp::Level::Debug;
  config.overflow = w_log_cpp::Overflow::OverrunOldest;

  if (!config.file_path.empty() && TryInitialize(config)) {
    g_initialized = true;
    g_mode = RTCWIN_LOG_MODE_FILE;
    w_log_cpp::setFlushOn(w_log_cpp::Level::Info);
    return 1;
  }

  if (!enable_console_fallback) return 0;
  config.enable_console = true;
  config.file_path.clear();
  if (!TryInitialize(config)) return 0;
  g_initialized = true;
  g_mode = RTCWIN_LOG_MODE_CONSOLE;
  w_log_cpp::setFlushOn(w_log_cpp::Level::Info);
  return 1;
}

void rtcwin_log_shutdown(void) {
  std::lock_guard<std::mutex> lock(g_mutex);
  if (g_initialized) w_log_cpp::shutdown();
  g_initialized = false;
  g_mode = RTCWIN_LOG_MODE_OFF;
}

int rtcwin_log_mode(void) {
  std::lock_guard<std::mutex> lock(g_mutex);
  return static_cast<int>(g_mode);
}

const char* rtcwin_log_last_error(void) {
  std::lock_guard<std::mutex> lock(g_mutex);
  return g_last_error.c_str();
}

void rtcwin_log_write(int level, const char* file, int line,
                      const char* function, const char* message) {
  if (message == nullptr) return;
  if (!g_initialized) return;
  w_log_cpp::logImpl("rtcwin", ToLevel(level),
                     file != nullptr ? file : "",
                     line,
                     function != nullptr ? function : "",
                     message, std::strlen(message));
}

}  // extern "C"
