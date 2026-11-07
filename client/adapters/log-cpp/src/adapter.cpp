#include "rtc_log_adapter.h"
#include <w_log_cpp.h>
#include <mutex>
#include <limits>

namespace {
std::mutex mutex;
bool initialized{};
constexpr const char* name = "rtc-rk-client";
bool valid_level(uint32_t level) { return level >= RTC_LOG_DEBUG && level <= RTC_LOG_ERROR; }
}  // namespace

extern "C" {
uint32_t rtc_log_abi_version(void) { return RTC_LOG_ABI_VERSION; }
int rtc_log_init(const rtc_log_options* options) {
  try {
    if (!options || options->struct_size < sizeof(rtc_log_options) ||
        options->abi_version != RTC_LOG_ABI_VERSION || !options->file_path || !*options->file_path ||
        !valid_level(options->level) || options->max_file_bytes < 65536 ||
        options->max_file_bytes > 64ULL * 1024 * 1024 || options->rotated_files > 32 ||
        options->queue_size < 128 || options->queue_size > 65536) return RTC_LOG_INVALID;
    std::lock_guard<std::mutex> lock(mutex);
    if (initialized) return RTC_LOG_CONFLICT;
    w_log_cpp::InitConfig config;
    config.name = name;
    config.file_path = options->file_path;
    config.enable_console = false;
    config.max_file_size = static_cast<size_t>(options->max_file_bytes);
    config.max_file_num = options->rotated_files;
    config.queue_size = options->queue_size;
    config.n_threads = 1;
    config.overflow = w_log_cpp::Overflow::OverrunOldest;
    config.level = config.file_level = static_cast<w_log_cpp::Level>(options->level);
    initialized = w_log_cpp::initNamed(name,config);
    if (initialized) w_log_cpp::setFlushOn(w_log_cpp::Level::Info);
    return initialized ? RTC_LOG_OK : RTC_LOG_UNAVAILABLE;
  } catch (...) { return RTC_LOG_INTERNAL; }
}
int rtc_log_write(uint32_t level, const char* utf8, size_t bytes) {
  try {
    if (!valid_level(level) || !utf8 || bytes == 0 || bytes > 4096) return RTC_LOG_INVALID;
    std::lock_guard<std::mutex> lock(mutex);
    if (!initialized) return RTC_LOG_UNAVAILABLE;
    w_log_cpp::logImpl(name,static_cast<w_log_cpp::Level>(level),"",0,"",utf8,bytes);
    return RTC_LOG_OK;
  } catch (...) { return RTC_LOG_INTERNAL; }
}
int rtc_log_is_initialized(void) {
  try { std::lock_guard<std::mutex> lock(mutex); return initialized ? 1 : 0; }
  catch (...) { return 0; }
}
void rtc_log_shutdown(void) {
  try {
    std::lock_guard<std::mutex> lock(mutex);
    if (initialized) { w_log_cpp::shutdown(); initialized = false; }
  } catch (...) {}
}
}  // extern "C"
