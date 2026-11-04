# 本地崩溃转储

`rtc-talk.exe` 在 `main` 开头、Qt 和 Engine 创建前安装 Breakpad，处理器保留到
Qt、窗口及 Engine 销毁之后。进程内线程发生未处理 Windows 异常时，写出
`%LOCALAPPDATA%\rtc-talk-win\crash_dumps\<GUID>.dmp`。LocalAppData 不可解析时
回退到 Windows TEMP 下同名子目录；初始化失败通过 OutputDebugString 报告，
不阻止客户端启动。目录在安装前创建并探测可写性。

## 依赖与 ABI

- 来源：用户编译的 `D:\google\breakpad_sdk`，Breakpad `9a2ad675`，MSVC v143 x64。
- 必要头文件、Debug/Release 的 exception_handler、crash_generation_client、common
  静态库复制到 `third_party/breakpad`；构建、运行不引用来源目录。
- `rtcwin_crash_handler.dll` 使用 /MT（Debug /MTd）链接静态库，Qt 程序使用
  /MD（Debug /MDd），只通过 C ABI 传递宽字符路径和不透明句柄。
- 不引入 SDK 的上传库、上传工具、server 库或预编译 DLL，不启动上传服务。
- 捕获回调只返回写入结果，不访问 Qt、不记录日志、不上传。
- 随包分发 `rtcwin_crash_handler.dll` 和 `Breakpad-LICENSE.txt`。

## 使用与分析

正常启动即可生效，无需打开开关。Breakpad 每次写入独立 GUID 文件；当前不自动
删除历史 dump。转储是原始诊断文件，不经过日志脱敏，不自动包含在诊断 ZIP 中。

Release 主程序、Engine 和捕获 DLL 已启用 /Zi 与 /DEBUG，PDB 位于对应
`build/windows-x64/bin/Release`。必须留存发生崩溃的同一构建 EXE/DLL 和 PDB，
在 Visual Studio 或 WinDbg 中打开 dmp 后加载匹配符号。SDK 或其他依赖内部的
源代码级分析还需要对应依赖的匹配符号；仅这三个 PDB 不保证所有第三方栈有行号。
当前便携运行包不包含 PDB，发布归档时单独保存符号。

## 捕获边界

- 这是进程内 Breakpad minidump，已覆盖独立主线程和工作线程访问违规。
- 主程序加载到 main 之前、处理器销毁之后的异常不在覆盖范围内；外部强杀、
  Windows fail-fast、严重进程损坏、被其他代码覆盖异常过滤器等也不保证写出 dump。
- `HANDLER_ALL` 的 SEH 过滤器是进程级的；invalid-parameter/purecall 的 CRT 钩子
  属于捕获 DLL 的静态 CRT，不应据此宣称覆盖 Qt /MD CRT 的所有终止路径。
- 调试器附加时异常先交给调试器，实际崩溃验证应在未附加调试器时运行。
- SDK 析构使用 TerminateThread，不支持将其作为动态反复安装/卸载的插件机制。
  本工程按进程启动安装一次，正常退出时销毁；不在 DllMain 中安装或销毁。

## 短链路验证

```powershell
ctest --test-dir build/windows-x64 -C Release -R '^rtcwin_crash_handler_smoke$' --output-on-failure
ctest --test-dir build/windows-x64 -C Debug -R '^rtcwin_crash_handler_smoke$' --output-on-failure
```

测试从 /MD 程序调用 /MT DLL，启动独立子进程分别在主线程、工作线程制造真实
访问违规，读取 dump 的 MDMP 文件头和 ExceptionStream，要求异常码为
`0xC0000005` 且线程 ID 有效。另验证无效路径返回失败。测试 dump 保留在
`build/windows-x64/crash-handler-smoke/<配置>`，不污染用户默认 dump 目录。
