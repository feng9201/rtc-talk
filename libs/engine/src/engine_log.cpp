#include "engine_log.h"

#include <filesystem>
#include <cstdlib>
#include <mutex>
#include <string>

#include <windows.h>

namespace rtcwin::engine {
namespace {

std::once_flag g_init_once;
std::string g_log_path;

std::string WideToUtf8(const std::wstring& value) {
  if (value.empty()) return {};
  const int size = WideCharToMultiByte(CP_UTF8, 0, value.data(),
                                      static_cast<int>(value.size()), nullptr,
                                      0, nullptr, nullptr);
  if (size <= 0) return {};
  std::string result(static_cast<std::size_t>(size), '\0');
  WideCharToMultiByte(CP_UTF8, 0, value.data(),
                      static_cast<int>(value.size()), result.data(), size,
                      nullptr, nullptr);
  return result;
}

std::string BuildLogPath() {
  char* path_override = nullptr;
  size_t path_override_length = 0;
  if (_dupenv_s(&path_override, &path_override_length,
                "RTCWIN_LOG_PATH") == 0 &&
      path_override != nullptr && path_override[0] != '\0') {
    std::string result(path_override);
    free(path_override);
    const std::filesystem::path path = std::filesystem::u8path(result);
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    return result;
  }
  if (path_override != nullptr) free(path_override);

  wchar_t* local_app_data = nullptr;
  size_t length = 0;
  std::wstring directory;
  if (_wdupenv_s(&local_app_data, &length, L"LOCALAPPDATA") == 0 &&
      local_app_data != nullptr) {
    directory.assign(local_app_data);
    free(local_app_data);
  } else {
    wchar_t module_path[MAX_PATH]{};
    const DWORD size = GetModuleFileNameW(nullptr, module_path, MAX_PATH);
    if (size > 0) {
      directory = std::filesystem::path(module_path).parent_path().wstring();
    } else {
      directory = L".";
    }
  }
  const std::filesystem::path log_dir =
      std::filesystem::path(directory) / L"rtc-talk-win" / L"logs";
  std::error_code error;
  std::filesystem::create_directories(log_dir, error);
  return WideToUtf8((log_dir / L"rtcwin.log").wstring());
}

}  // namespace

void InitializeEngineLogging() {
  std::call_once(g_init_once, [] {
    g_log_path = BuildLogPath();
    const bool initialized =
        rtcwin_log_initialize(g_log_path.c_str(), 1) != 0;
    if (initialized && rtcwin_log_mode() == RTCWIN_LOG_MODE_CONSOLE) {
      g_log_path = "<console>";
    }
    if (initialized) {
      LOG_INFO("rtcwin logging initialized path={}", g_log_path);
    }
  });
}

void ShutdownEngineLogging() {
  if (rtcwin_log_mode() != RTCWIN_LOG_MODE_OFF) {
    LOG_INFO("rtcwin logging shutdown");
    rtcwin_log_shutdown();
  }
}

std::string EngineLogPath() { return g_log_path; }

void WriteEngineLog(int level, const char* file, int line,
                    const char* function, const char* message) {
  rtcwin_log_write(level, file, line, function, message);
}

}  // namespace rtcwin::engine
