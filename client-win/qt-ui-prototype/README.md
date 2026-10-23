# RTC Talk Qt Widgets UI Prototype

这是独立于 `client-win` 现有主工程的 Qt Widgets GUI 原型，用于先行搭建和调试桌面界面。
当前版本只包含 mock 数据和 mock 交互，不依赖 WebRTC、CoreClient、信令或真实设备。

## 构建

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH=C:/Qt/5.15.2/msvc2019_64
cmake --build build --config RelWithDebInfo --parallel
```

生成文件位于 `build/RelWithDebInfo/rtc_talk_qt_ui.exe`。

## 运行与验收

```powershell
# 默认进入入房页
.\build\RelWithDebInfo\rtc_talk_qt_ui.exe

# 直接打开指定原型场景
.\build\RelWithDebInfo\rtc_talk_qt_ui.exe --scenario multi
.\build\RelWithDebInfo\rtc_talk_qt_ui.exe --scenario one
.\build\RelWithDebInfo\rtc_talk_qt_ui.exe --scenario sharing
.\build\RelWithDebInfo\rtc_talk_qt_ui.exe --scenario devices
.\build\RelWithDebInfo\rtc_talk_qt_ui.exe --scenario diagnostics

# 冒烟运行并截图
.\build\RelWithDebInfo\rtc_talk_qt_ui.exe --scenario multi --smoke-seconds 2 --screenshot ui.png
```

## 接入边界

后续接入建议保持以下方向：

- 页面和面板只依赖 `UiController`，不要直接调用 WebRTC/CoreClient。
- 远端/本地视频帧统一送入 `VideoSurface::setFrame(const QImage&)`。
- 真实视频渲染可继承 `VideoSurface`，替换 `MockVideoSurface`，不需要改动布局和交互层。
- 入房参数集中在 `JoinOptions`；控制动作集中在 `UiController` 的 `joinRoom`、`setMicEnabled`、
  `setCameraEnabled`、`setScreenShareEnabled`、`setDeafen`、`leaveRoom`。
- 后续把 `UiController` 的 mock 实现替换成 CoreClient 适配器即可。

## 当前覆盖

- 入房前配置、摄像头 mock 预览、入房摘要
- 多人宫格、1v1 大画面 + 本地画中画、屏幕共享布局
- 成员/设备/状态右侧面板
- 成员操作菜单、全员静音
- 麦克风、摄像头、共享、收听、采集源、设备选择等 mock 状态
- 设置弹窗、离开确认、诊断抽屉、Toast
- `--scenario`、`--smoke-seconds`、`--screenshot`
