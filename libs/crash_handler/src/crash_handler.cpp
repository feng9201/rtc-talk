#include "rtcwin/crash_handler/crash_handler.h"

#include <windows.h>
#include <shlobj.h>

#include "client/windows/handler/exception_handler.h"

#include <filesystem>
#include <memory>
#include <string>

struct rtcwin_crash_handler {
  std::unique_ptr<google_breakpad::ExceptionHandler> handler;
};

namespace {

std::filesystem::path DefaultDirectory() {
  PWSTR local_app_data = nullptr;
  const HRESULT result = SHGetKnownFolderPath(
      FOLDERID_LocalAppData, KF_FLAG_DEFAULT, nullptr, &local_app_data);
  std::filesystem::path base;
  if (SUCCEEDED(result) && local_app_data != nullptr) base = local_app_data;
  CoTaskMemFree(local_app_data);
  if (base.empty()) base = std::filesystem::temp_directory_path();
  return base / L"rtc-talk-win" / L"crash_dumps";
}

bool DumpWritten(const wchar_t*, const wchar_t*, void*, EXCEPTION_POINTERS*,
                 MDRawAssertionInfo*, bool succeeded) {
  // No Qt, logging, allocation or network operations on the crash path.
  return succeeded;
}

}  // namespace

extern "C" {

rtcwin_crash_handler* rtcwin_crash_handler_create(const wchar_t* dump_directory) {
  try {
    const std::filesystem::path directory =
        dump_directory != nullptr ? std::filesystem::path(dump_directory)
                                  : DefaultDirectory();
    if (!directory.is_absolute()) return nullptr;
    std::filesystem::create_directories(directory);
    if (!std::filesystem::is_directory(directory)) return nullptr;
    // Detect unwritable directories at startup, before installing the handler.
    wchar_t probe[MAX_PATH]{};
    if (!GetTempFileNameW(directory.c_str(), L"rtc", 0, probe)) return nullptr;
    if (!DeleteFileW(probe)) return nullptr;
    auto result = std::make_unique<rtcwin_crash_handler>();
    result->handler = std::make_unique<google_breakpad::ExceptionHandler>(
        directory.wstring(), nullptr, &DumpWritten, nullptr,
        google_breakpad::ExceptionHandler::HANDLER_ALL);
    return result.release();
  } catch (...) {
    return nullptr;
  }
}

void rtcwin_crash_handler_destroy(rtcwin_crash_handler* handler) {
  delete handler;
}

}  // extern "C"
