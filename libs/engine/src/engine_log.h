#pragma once

#include "rtcwin/log_adapter/log_api.h"

#include <fmt/format.h>

#include <string>
#include <utility>

namespace rtcwin::engine {

void InitializeEngineLogging();
void ShutdownEngineLogging();
std::string EngineLogPath();
void WriteEngineLog(int level, const char* file, int line,
                    const char* function, const char* message);

template <typename... Args>
void LogFormatted(int level, const char* file, int line,
                  const char* function, const char* format,
                  Args&&... args) noexcept {
  try {
    const std::string message =
        fmt::format(format, std::forward<Args>(args)...);
    WriteEngineLog(level, file, line, function, message.c_str());
  } catch (...) {
    // Diagnostics must never terminate the media engine.
  }
}

}  // namespace rtcwin::engine

#define LOG_TRACE(...)                                                   \
  ::rtcwin::engine::LogFormatted(RTCWIN_LOG_TRACE, __FILE__, __LINE__,  \
                                 __FUNCTION__, __VA_ARGS__)
#define LOG_DEBUG(...)                                                   \
  ::rtcwin::engine::LogFormatted(RTCWIN_LOG_DEBUG, __FILE__, __LINE__,  \
                                 __FUNCTION__, __VA_ARGS__)
#define LOG_INFO(...)                                                    \
  ::rtcwin::engine::LogFormatted(RTCWIN_LOG_INFO, __FILE__, __LINE__,   \
                                 __FUNCTION__, __VA_ARGS__)
#define LOG_WARN(...)                                                    \
  ::rtcwin::engine::LogFormatted(RTCWIN_LOG_WARN, __FILE__, __LINE__,   \
                                 __FUNCTION__, __VA_ARGS__)
#define LOG_ERR(...)                                                     \
  ::rtcwin::engine::LogFormatted(RTCWIN_LOG_ERROR, __FILE__, __LINE__,  \
                                 __FUNCTION__, __VA_ARGS__)
#define LOG_CRITICAL(...)                                                \
  ::rtcwin::engine::LogFormatted(RTCWIN_LOG_CRITICAL, __FILE__,         \
                                 __LINE__, __FUNCTION__, __VA_ARGS__)
