#ifndef RTC_LOG_ADAPTER_H
#define RTC_LOG_ADAPTER_H
#include <stddef.h>
#include <stdint.h>
#if defined(_WIN32)
#if defined(RTC_LOG_BUILD)
#define RTC_LOG_API __declspec(dllexport)
#else
#define RTC_LOG_API __declspec(dllimport)
#endif
#else
#define RTC_LOG_API __attribute__((visibility("default")))
#endif
#ifdef __cplusplus
extern "C" {
#endif

#define RTC_LOG_ABI_VERSION 1u
enum rtc_log_level {
  RTC_LOG_DEBUG = 1, RTC_LOG_INFO = 2, RTC_LOG_WARN = 3, RTC_LOG_ERROR = 4
};
enum rtc_log_result {
  RTC_LOG_OK = 0, RTC_LOG_INVALID = 1, RTC_LOG_CONFLICT = 2,
  RTC_LOG_UNAVAILABLE = 3, RTC_LOG_INTERNAL = 4
};
typedef struct rtc_log_options {
  uint32_t struct_size;
  uint32_t abi_version;
  const char* file_path; /* UTF-8; copied during init. Parent must exist. */
  uint64_t max_file_bytes;
  uint32_t rotated_files;
  uint32_t queue_size;
  uint32_t level;
} rtc_log_options;

/* One process-wide logger; application owns init/shutdown, modules only write.
 * No console output. Queue overflow drops oldest. Functions are thread-safe.
 * No STL/fmt/spdlog types, allocations or exceptions cross this C boundary. */
RTC_LOG_API uint32_t rtc_log_abi_version(void);
RTC_LOG_API int rtc_log_init(const rtc_log_options* options);
RTC_LOG_API int rtc_log_write(uint32_t level, const char* utf8, size_t bytes);
RTC_LOG_API int rtc_log_is_initialized(void);
/* Drains the async queue. Call after media/UI workers have stopped. */
RTC_LOG_API void rtc_log_shutdown(void);

#ifdef __cplusplus
}
#endif
#endif
