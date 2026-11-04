# Breakpad Windows Client SDK 使用说明

版本：breakpad @ 9a2ad675（2026-09-23）· x64 · MSVC v143 · 使用文档与 `manifest\build_info.txt` 配套。

## 1. 目录结构

```
breakpad_sdk\
  include\                    头文件闭包（include 根目录，33 个）
  lib\x64\Release\            静态库（/MT）
  lib\x64\Debug\              静态库（/MTd）
  lib\x64\Release\dll\        breakpad_client.dll + 导入库 + pdb
  lib\x64\Debug\dll\          breakpad_client.dll + 导入库 + pdb
  bin\x64\                    dump_syms.exe / symupload.exe（官方预编译）
  manifest\build_info.txt     构建信息
```

## 2. 接入方式总览

| 需求 | 方式 | 链接内容 | 额外依赖 |
|---|---|---|---|
| 只抓崩溃 dmp | A. DLL（推荐） | `lib\x64\Release\dll\breakpad_client.lib` | 无（运行时带 DLL） |
| 只抓崩溃 dmp | B. 静态库 | `exception_handler.lib + crash_generation_client.lib + common.lib` | 无 |
| 进程外服务端 | C. 静态库 | 方式 B + `crash_generation_server.lib` | 无 |
| 崩溃报告上传 | D. 静态库 | `crash_report_sender.lib + common.lib` | `wininet.lib` |

> Debug 配置全部换用 `lib\x64\Debug\` 对应文件。**CRT 必须与消费方一致**（库是 `/MT`/`/MTd` 编译）。

## 3. 方式 A：DLL（推荐，抓 dmp）

```cpp
#include "client/windows/handler/exception_handler.h"

// 崩溃回调：在 dmp 写出后调用
bool MinidumpCallback(const wchar_t* dump_path, const wchar_t* minidump_id,
                      void* context, EXCEPTION_POINTERS* exinfo,
                      MDRawAssertionInfo* assertion, bool succeeded) {
  if (succeeded) {
    // 拼接 dump_path\minidump_id.dmp 即得 minidump 文件
  }
  return succeeded;  // true=不弹出 Windows 错误报告框
}

// 安装（通常放 main 开头）：
static google_breakpad::ExceptionHandler handler(
    L"C:\\dumps",                    // dump 输出目录
    nullptr,                         // filter 回调（可空）
    MinidumpCallback,                // 写出后回调
    nullptr,                         // 回调 context
    google_breakpad::ExceptionHandler::HANDLER_ALL);  // 挂全部异常类型
```

**编译/链接**：

```bat
cl /c /std:c++20 /MT /EHsc /I D:\google\breakpad_sdk\include app.cpp
link app.obj D:\google\breakpad_sdk\lib\x64\Release\dll\breakpad_client.lib
```

**运行**：`breakpad_client.dll` 需与 exe 同目录或在 PATH 中；发布时随 exe 一起分发。

## 4. 方式 B：静态库（抓 dmp）

代码同方式 A，链接改为：

```bat
link app.obj ^
  D:\google\breakpad_sdk\lib\x64\Release\exception_handler.lib ^
  D:\google\breakpad_sdk\lib\x64\Release\crash_generation_client.lib ^
  D:\google\breakpad_sdk\lib\x64\Release\common.lib
```

无需分发任何 DLL。

## 5. 方式 C：进程外服务端（可选）

在独立进程/服务中创建 `CrashGenerationServer`（监听命名管道），崩溃进程通过
`CrashGenerationClient` 连接。需另链 `crash_generation_server.lib`：

```cpp
#include "client/windows/crash_generation/crash_generation_server.h"
#include "client/windows/crash_generation/crash_generation_client.h"
```

## 6. 方式 D：崩溃报告上传（可选）

用 `CrashReportSender` 将已生成的 dmp 上传（走 `HTTPUpload`，WinInet 实现）：

```cpp
#include "client/windows/sender/crash_report_sender.h"
// 链接：crash_report_sender.lib + common.lib + wininet.lib
```

## 7. 工具

```bat
:: 从 PE 生成 breakpad 符号文件（供 minidump_stackwalk 还原堆栈）
dump_syms.exe app.pdb > app.sym

:: 上传符号文件到符号服务器
symupload.exe app.sym https://your-symbol-server/
```

符号文件与 minidump 配对，用 `minidump_stackwalk`（Linux 工具）即可还原崩溃堆栈。

## 8. 已知限制与注意事项

- **CRT 必须匹配**：库用 `/MT`（Debug 为 `/MTd`）编译。消费方若用 `/MD` 会 LNK2038；DLL 模式下 `std::wstring` 跨边界传参，CRT 不一致会导致堆不匹配崩溃。
- **建议宏**：消费方建议定义 `WIN32_LEAN_AND_MEAN;NOMINMAX;UNICODE;_UNICODE`（库本身如此编译，保持一致的 `_WIN32_WINNT=0x0A00`）。
- **回调在线程上执行**：minidump 回调运行在异常线程，勿在其中做重活（如网络上传），建议只记录路径。
- **HANDLER_ALL**：进程内模式，无需 server 进程；进程外模式需另起 server 进程。
- **上传部分不引入**：只抓 dmp 的接入（A/B）不会把 `http_upload` 代码编进程序，无 wininet 依赖（已验证导入表仅 KERNEL32.dll）。
