#pragma once

#include <stddef.h>

#if defined(_WIN32)
#if defined(RTCWIN_LOG_ADAPTER_EXPORTS)
#define RTCWIN_LOG_API __declspec(dllexport)
#else
#define RTCWIN_LOG_API __declspec(dllimport)
#endif
#else
#define RTCWIN_LOG_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef enum rtcwin_log_level {
  RTCWIN_LOG_TRACE = 0,
  RTCWIN_LOG_DEBUG = 1,
  RTCWIN_LOG_INFO = 2,
  RTCWIN_LOG_WARN = 3,
  RTCWIN_LOG_ERROR = 4,
  RTCWIN_LOG_CRITICAL = 5,
} rtcwin_log_level;

typedef enum rtcwin_log_mode_t {
  RTCWIN_LOG_MODE_OFF = 0,
  RTCWIN_LOG_MODE_FILE = 1,
  RTCWIN_LOG_MODE_CONSOLE = 2,
} rtcwin_log_mode_t;

// Initializes the vcpkg-provided log-cpp runtime. If file_path cannot be
// opened, the adapter falls back to a console sink.
RTCWIN_LOG_API int rtcwin_log_initialize(const char* file_path,
                                         int enable_console_fallback);
RTCWIN_LOG_API void rtcwin_log_shutdown(void);
RTCWIN_LOG_API int rtcwin_log_mode(void);
RTCWIN_LOG_API const char* rtcwin_log_last_error(void);
RTCWIN_LOG_API void rtcwin_log_write(int level, const char* file, int line,
                                     const char* function,
                                     const char* message);

#ifdef __cplusplus
}
#endif
