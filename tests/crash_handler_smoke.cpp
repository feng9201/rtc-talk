#include "rtcwin/crash_handler/crash_handler.h"

#include <windows.h>
#include <dbghelp.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace fs = std::filesystem;
using Handler = std::unique_ptr<rtcwin_crash_handler,
                                decltype(&rtcwin_crash_handler_destroy)>;

__declspec(noinline) void Crash() {
  *reinterpret_cast<volatile int*>(static_cast<uintptr_t>(1)) = 42;
}

bool CheckDump(const fs::path& directory) {
  int count = 0;
  for (const auto& entry : fs::directory_iterator(directory)) {
    if (entry.path().extension() != L".dmp") continue;
    ++count;
    std::ifstream input(entry.path(), std::ios::binary);
    std::vector<char> bytes((std::istreambuf_iterator<char>(input)), {});
    if (bytes.size() < sizeof(MINIDUMP_HEADER)) return false;
    const auto* header = reinterpret_cast<const MINIDUMP_HEADER*>(bytes.data());
    if (header->Signature != MINIDUMP_SIGNATURE) return false;
    PMINIDUMP_DIRECTORY stream = nullptr;
    void* data = nullptr;
    ULONG size = 0;
    if (!MiniDumpReadDumpStream(bytes.data(), ExceptionStream, &stream, &data,
                                &size) ||
        size < sizeof(MINIDUMP_EXCEPTION_STREAM)) return false;
    const auto* exception = static_cast<MINIDUMP_EXCEPTION_STREAM*>(data);
    if (exception->ExceptionRecord.ExceptionCode != EXCEPTION_ACCESS_VIOLATION ||
        exception->ThreadId == 0) return false;
    std::wcout << L"valid access-violation dump: " << entry.path() << L'\n';
  }
  return count == 1;
}

bool RunChild(const fs::path& executable, const fs::path& directory,
              const std::wstring& mode) {
  fs::create_directories(directory);
  std::wstring command = L"\"" + executable.wstring() + L"\" " + mode +
                         L" \"" + directory.wstring() + L"\"";
  STARTUPINFOW startup{};
  startup.cb = sizeof(startup);
  PROCESS_INFORMATION process{};
  if (!CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr,
                      FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup,
                      &process)) return false;
  const DWORD wait = WaitForSingleObject(process.hProcess, 15000);
  DWORD code = 0;
  GetExitCodeProcess(process.hProcess, &code);
  if (wait != WAIT_OBJECT_0) {
    TerminateProcess(process.hProcess, 99);
    WaitForSingleObject(process.hProcess, 5000);
  }
  CloseHandle(process.hThread);
  CloseHandle(process.hProcess);
  return wait == WAIT_OBJECT_0 && code != 0 && CheckDump(directory);
}

int wmain(int argc, wchar_t** argv) {
  SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
  if (argc == 3 && std::wstring(argv[1]).find(L"--child-") == 0) {
    Handler handler(rtcwin_crash_handler_create(argv[2]),
                    &rtcwin_crash_handler_destroy);
    if (!handler) return 2;
    if (std::wstring(argv[1]) == L"--child-worker") {
      std::thread worker(Crash);
      worker.join();
    } else {
      Crash();
    }
    return 0;
  }
  if (argc != 2) return 2;
  const fs::path root = fs::absolute(argv[1]) /
      (std::to_wstring(GetCurrentProcessId()) + L"-" +
       std::to_wstring(GetTickCount64()));
  fs::create_directories(root);
  std::cerr << "checking invalid directories\n";
  // Failure must be returned across the C ABI without an exception.
  const fs::path blocked = root / L"not-a-directory";
  std::ofstream(blocked) << "file";
  Handler invalid(rtcwin_crash_handler_create(blocked.c_str()),
                  &rtcwin_crash_handler_destroy);
  Handler relative(rtcwin_crash_handler_create(L"relative-path"),
                   &rtcwin_crash_handler_destroy);
  if (invalid || relative) return 3;
  wchar_t executable[32768]{};
  if (!GetModuleFileNameW(nullptr, executable, 32768)) return 6;
  std::cerr << "crashing main-thread child\n";
  if (!RunChild(executable, root / L"main-thread", L"--child-main")) return 7;
  std::cerr << "crashing worker-thread child\n";
  if (!RunChild(executable, root / L"worker-thread", L"--child-worker")) return 8;
  std::cout << "crash handler smoke: PASS\n";
  return 0;
}
