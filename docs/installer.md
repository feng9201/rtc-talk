# Inno Setup 安装打包

## 一键入口

```powershell
.\build.bat -p
```

流程：检查工具 → Version.txt 第四位 +1 → CMake 配置 → Release 编译 →
整理运行时到 `build/windows-x64/package` → 生成 ZIP → 编译根目录 `rtc-talk.iss`。
默认使用本项目已有 Qt/vcpkg/WebRTC 环境路径，支持环境变量覆盖；无需引用
`D:\work\device_tool`。参考工程只用于了解打包流程。

需要 Inno Setup 6（支持 x64compatible 的版本，6.3 或更高）。优先使用 `ISCC`
环境变量，其次 PATH，再查找 Program Files 下的 Inno Setup 6：

```powershell
$env:ISCC = 'C:\Program Files (x86)\Inno Setup 6\ISCC.exe'
.\build.bat -p
```

输出：

- `dist/rtc-talk-win_Setup_<版本>_x64.exe`
- `dist/rtc-talk-win-<时间戳>.zip`
- `build/windows-x64/package/VERSION.txt` 与 SHA256SUMS.txt，记录产物版本、提交及工作树状态。

## 版本规则

根目录 `Version.txt` 是唯一产品版本源，初始版本为 `1.0.0.0`。仅 `build.bat -p`
（或显式 `-Installer -IncrementVersion`）递增第四位，首次为 `1.0.0.1`。
普通 Release/Debug 构建、CMake 自动重新配置均不递增，不使用会残留在 CMake
缓存中的 INCREMENT_VERSION 开关。每一位必须为 0–65535。

同一版本用于 EXE/DLL 资源、Qt applicationVersion、Engine 版本接口、日志和
信令 hello 的客户端版本。安装器从已部署 EXE 的版本资源读取版本。
Qt Widgets 图标仍使用 `resources/icon.ico`。新的 CMake RC 模板替代原固定版本
`resources/app.rc`，第三方 DLL 保持其原始版本。

打包前检查 EXE 和本项目三个 DLL 的版本与 Version.txt 相同，避免混装。
版本号在编译前递增，后续失败不自动回退；下一次 `-p` 会消耗下一个构建号。

修复构建/打包问题后若要复用已分配版本，不要再次运行 `-p`：

```powershell
.\build.bat
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\package.ps1 -NoBuild -Installer
```

仅重新打包已完成的版本：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\scripts\package.ps1 -NoBuild -Installer
```

## 安装与卸载

- 产品名 `rtc-talk-win`，入口 `rtc-talk.exe`，使用独立且固定的 AppId。
- x64 Windows 10 22H2 / Windows 11，默认安装到 Program Files，默认管理员模式。
- 支持 Inno 标准 `/DIR=`，也保留模板的 `/InstallPath=` 自定义路径参数；
  `/CURRENTUSER` 可用于当前用户安装。
- 创建开始菜单入口，桌面快捷方式可选，默认不开机自启动。
- 安装后可选启动；静默安装不启动程序。
- 使用 Inno Restart Manager 提示关闭占用文件的应用，不强杀模板中的其他项目进程。
- 覆盖安装替换受管理文件；不递归删除用户选定目录，不删除未知用户文件。
- 卸载删除安装器管理的文件与注册信息，保留用户日志、dmp、设置、Credential Manager 凭证。
- 只收集整理后的运行时目录；不含测试 EXE、PDB、日志或 dmp。Breakpad DLL、许可证、
  Qt 插件、VC Runtime、FFmpeg/fmt/log-cpp 均随包分发。匹配 PDB 在构建目录单独归档。

## 当前验证

首包 `1.0.0.1` 已完成：Release/Debug 构建、两种配置 13/13 CTest；普通构建版本
保持不变；版本递增与 65535 溢出保护；隔离目录当前用户静默安装；仅系统目录 PATH
下启动；同版本覆盖安装；卸载与用户文件保留。测试日志位于
`build/windows-x64/installer-smoke`，测试安装已卸载。

这是本机安装链路验证。安装包尚未签名，未完成跨版本升级、降级/回滚以及干净
Windows 10/11 发布验收，也未新增真实服务联调或长稳测试。
