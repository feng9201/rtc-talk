#include "rtcwin/log_adapter/log_api.h"

#include <cstdio>
#include <filesystem>
#include <string>

int main() {
  const std::filesystem::path path =
      std::filesystem::temp_directory_path() / "rtcwin-log-adapter-smoke.log";
  std::error_code error;
  std::filesystem::remove(path, error);
  const std::string text = path.u8string();
  std::printf("before init\n");
  if (rtcwin_log_initialize(text.c_str(), 1) == 0) {
    std::printf("init failed: %s\n", rtcwin_log_last_error());
    return 1;
  }
  std::printf("after init mode=%d\n", rtcwin_log_mode());
  rtcwin_log_write(RTCWIN_LOG_INFO, __FILE__, __LINE__, __FUNCTION__,
                   "adapter smoke");
  std::printf("after write\n");
  rtcwin_log_shutdown();
  std::printf("after shutdown\n");
  return 0;
}
