# RTC Talk Qt Widgets UI — 技术交接与业务接入指南

> 项目：`client-win/qt-ui-prototype`  
> 版本：GUI mock 第一版  
> 状态：M1–M6 已完成并确认  
> 更新日期：2026-10-23  
> 适用对象：后续负责 CoreClient、WebRTC、设备、信令和桌面端集成的开发人员

## 1. 文档目的

本文档说明当前 Qt Widgets GUI 原型：

- 已经实现了什么
- 代码按什么结构组织
- UI 状态如何流转
- 哪些接口是 mock，哪些接口是稳定的 UI 边界
- 后续如何接入真实 CoreClient/WebRTC
- 接入时需要重点处理哪些线程、生命周期和渲染问题
- 如何构建、运行、截图和做冒烟验证

当前项目是独立的 GUI 原型工程，不是最终产品工程。后续接入真实业务时，应尽量保持 UI 页面、布局和信号连接不变，只替换数据来源和媒体实现。

## 2. 当前交付结论

### 2.1 已确认完成的模块

| 模块 | 内容 | 状态 |
|---|---|---|
| M1 | 应用壳、页面切换、无边框窗口、房间页骨架 | 已确认 |
| M2 | 宫格、聚焦、1v1、屏幕共享、视频过滤 | 已确认 |
| M3 | 麦克风、摄像头、共享、收听、设置入口、离开确认 | 已确认 |
| M4 | 成员、设备、状态面板及业务交互 | 已确认 |
| M5 | 设置弹窗、离开弹窗、诊断抽屉、视觉与响应式收口 | 已确认 |
| M6 | 六场景回归、尺寸/缩放、冒烟、交接文档 | 已确认 |

### 2.2 当前产物

```text
client-win/qt-ui-prototype/
  CMakeLists.txt
  README.md
  docs/
    DELIVERY_PLAN.md
    HANDOFF.md
  resources/
  src/
  build/RelWithDebInfo/rtc_talk_qt_ui.exe
```

当前可执行文件：

```text
build/RelWithDebInfo/rtc_talk_qt_ui.exe
```

### 2.3 当前不包含的内容

- libwebrtc 引擎接入
- CoreClient/业务 SDK 接入
- 信令、房间协议、鉴权
- 真实设备枚举和媒体开关
- 真实远端/本地视频帧
- 真实 `getStats`
- 配置持久化
- 安装包、签名、自动更新

## 3. 构建环境

### 3.1 必需环境

- Windows 10/11 x64
- Visual Studio 2022
- Qt 5.15.2，编译器工具链为 `msvc2019_64`
- CMake 3.16 或更高
- C++17

### 3.2 首次配置

```powershell
cd D:\google\webrtc-win-client\rtc-talk\client-win\qt-ui-prototype
cmake -S . -B build -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH=C:/Qt/5.15.2/msvc2019_64
```

### 3.3 构建

```powershell
cmake --build .\build --config RelWithDebInfo --parallel
```

### 3.4 构建说明

- `CMakeLists.txt` 使用递归 `file(GLOB_RECURSE ... CONFIGURE_DEPENDS)`，新增 `src/*.h` 和 `src/*.cpp` 会自动加入目标。
- Qt 使用 `AUTOMOC`、`AUTORCC`、`AUTOUIC`。
- Windows 下使用 `/utf-8`，因为源码含大量中文界面文案。
- 链接 `user32` 和 `dwmapi`，用于无边框窗口原生行为。
- `LNK4099 qtmain.pdb` 是 Qt 安装包自身的非阻塞警告，不影响运行。

## 4. 代码结构与职责

```text
src/
  main.cpp                  QApplication、命令行参数、场景和截图入口
  MainWindow.h/.cpp         QStackedWidget、页面切换、窗口控制、Controller 连接
  app/
    UiState.h               数据结构：成员、入房参数、UI 状态
    UiController.h/.cpp     mock 业务控制器与 mock 数据生成
  base/
    FramelessWindow.h/.cpp  无边框窗口拖动、缩放、最小/最大化
    Theme.h/.cpp            颜色/图标绘制、通用按钮、Label、Card 工厂
  pages/
    PrejoinPage.h/.cpp      入房配置页、摄像头预览、JoinOptions 生成
    RoomPage.h/.cpp         房间页全部布局、侧栏、弹窗、诊断和信号
  video/
    VideoSurface.h/.cpp     视频渲染抽象边界，接收 QImage 帧
    MockVideoSurface.h/.cpp 当前 mock 视频画面
    VideoTile.h/.cpp        单个视频成员卡片、状态覆盖层、选择/双击
    VideoGrid.h/.cpp        宫格、聚焦、1v1、共享布局
  widgets/
    Avatar.h/.cpp           首字母头像
    ControlButton.h/.cpp    控制栏图标 + 文案
    IconButton.h/.cpp       圆形/小尺寸图标按钮
    Pill.h/.cpp             状态胶囊
    SegmentedControl.h/.cpp 分段切换控件
    Toast.h/.cpp            轻量提示
    WindowControls.h/.cpp   最小化、最大化/还原、关闭
resources/
  resources.qrc
  theme.qss
  icons/chevron-down.png
```

## 5. 启动、导航和生命周期

### 5.1 启动流程

```text
main()
  → 创建 QApplication
  → Theme::apply()
  → 创建 MainWindow
  → MainWindow 创建 PrejoinPage 和 RoomPage
  → QStackedWidget 默认显示 PrejoinPage
```

### 5.2 入房流程

```text
PrejoinPage::joinRequested(JoinOptions)
  → UiController::join(options)
  → UiController 生成 mock members/state
  → emit joined()
  → MainWindow 立即切换 RoomPage
  → RoomPage::setState(controller.state())
  → emit stateChanged()
  → MainWindow 再次同步 RoomPage、statsJson()
```

重要约定：

- `UiController::join()` 先发 `joined()`，再发 `stateChanged()`。
- MainWindow 收到 `joined()` 时必须立即切换到房间页，否则第二次状态同步会落在隐藏页上。
- 当前 `UiState::members` 已经包含本人，不能再次追加本人。
- 本人固定 ID 为 `"me"`，通过 `MemberInfo::isSelf == true` 标识。

### 5.3 离开流程

```text
RoomPage 离开按钮
  → showLeaveConfirmation()
  → 用户确认
  → emit leaveRequested()
  → UiController::leaveRoom()
  → emit left()
  → MainWindow 切回 PrejoinPage
  → emit stateChanged()
```

当前离开弹窗是无边框模态弹窗，固定居中，不支持拖动。

### 5.4 原型场景

`UiController::startScenario(Scenario)` 会先 `join()`，然后根据场景执行共享、切换面板或打开诊断。

`UiController::setScenario(Scenario)` 只切换成员/场景，不执行入房。

## 6. 数据契约

### 6.1 `JoinOptions`

```cpp
struct JoinOptions {
    QString server;         // 信令服务
    QString userId;         // 用户 ID
    QString roomId;         // 房间 ID
    QString token;          // 入房 token，仅保存在内存
    QString audioBackend;   // 音频后端描述
    QString modePref;       // auto / p2p / sfu
    QString talkMode;       // free / ptt / moderated
    Scenario scenario;      // 原型演示场景
    bool fetchDevToken;     // 开发测试：从 /dev/token 获取凭证
    QString source;         // mic / system
    int volume;             // 0–100
    QString micDevice;
    QString playoutDevice;
    QString cameraDevice;
};
```

### 6.2 `MemberInfo`

```cpp
struct MemberInfo {
    QString id;
    QString name;
    QString role;      // host / member
    bool mic;
    bool camera;
    bool poor;
    bool speaking;
    bool isSelf;
    QString source;    // mic / system / screen
};
```

### 6.3 `UiState`

`UiState` 是当前 UI 的单一渲染输入，主要包括：

- 房间标题、房间 ID、连接状态
- 媒体模式、通话模式、是否 TURN 中继
- 成员列表、当前选中成员
- 音频源、音量、麦克风/播放/摄像头设备
- 本人麦克风、摄像头、共享、静音收听、预览状态
- 当前面板 Tab、视频过滤、宫格/聚焦布局
- 入房时长、日志和 JoinOptions

### 6.4 数据流原则

真实业务接入后仍应遵守：

```text
业务/媒体后端
  → CoreUiAdapter / UiController
  → UiState
  → MainWindow / RoomPage
```

不要让 `RoomPage`、`VideoTile` 或 `PrejoinPage` 直接持有 WebRTC/CoreClient 对象。

## 7. `UiController` 接口契约

当前 `UiController` 是纯 mock 实现，但它的接口应作为后续业务适配层保持稳定。

### 7.1 公共槽函数

| 槽函数 | 用途 |
|---|---|
| `join(const JoinOptions&)` | 入房 |
| `leaveRoom()` | 离房 |
| `startScenario(Scenario)` | 直接启动入房后的演示场景 |
| `toggleMic()` | 开关麦克风 |
| `toggleCamera()` | 开关摄像头 |
| `toggleShare()` | 开始/停止屏幕共享 |
| `toggleDeafen()` | 开关本机静音收听 |
| `setSource(QString)` | 麦克风/系统音互斥切换 |
| `selectMember(QString)` | 选择成员 |
| `setPanelTab(PanelTab)` | 切换成员/设备/状态面板 |
| `setStageFilter(StageFilter)` | 全部成员/仅视频成员 |
| `setStageLayout(StageLayout)` | 宫格/聚焦 |
| `muteAll()` | 主持人全员静音 |
| `memberAction(id, action)` | 强制闭麦/授予发言/移出成员 |
| `setMicDevice(QString)` | 麦克风设备 |
| `setPlayoutDevice(QString)` | 播放设备 |
| `setCameraDevice(QString)` | 摄像头设备 |
| `setVolume(int)` | 播放音量 |
| `setPreview(bool)` | 本地摄像头预览 |
| `refreshDevices()` | 刷新设备 |
| `openDiagnostics()` | 打开诊断 |
| `closeDiagnostics()` | 关闭诊断 |
| `setScenario(Scenario)` | 不重新入房地切换场景 |

### 7.2 信号

| 信号 | 用途 |
|---|---|
| `joined()` | 入房成功，要求 MainWindow 立即切页 |
| `left()` | 离房成功，要求 MainWindow 返回入房页 |
| `stateChanged()` | 任何 UI 可见状态变化 |
| `toastRequested(text, tone)` | 通用提示 |
| `elapsedChanged(seconds)` | 通话计时 |
| `diagnosticsChanged(bool)` | 诊断开关 |

### 7.3 当前 mock 行为

- 房间成员：`me`、`u1`–`u5`
- `me` 固定为本人
- 多人场景本人为 `host`
- 1v1 场景本人为 `member`
- 每 7 秒模拟 `u1` 说话
- `statsJson()` 返回 mock 统计和房间/本人状态
- 日志最多保留 40 条

### 7.4 入房/离房顺序

不要修改以下顺序：

```text
join():
  state.joined = true
  emit joined()
  emit stateChanged()

leaveRoom():
  state.joined = false
  emit left()
  emit stateChanged()
```

## 8. MainWindow 信号映射

`MainWindow` 是页面和 Controller 的组装层，不承载业务逻辑。

### 8.1 PrejoinPage 到 Controller

| UI 信号 | 目标 |
|---|---|
| `joinRequested(JoinOptions)` | `UiController::join` |
| `cameraPreviewToggled(bool)` | `UiController::setPreview` |
| `saveSettingsRequested(JoinOptions)` | Toast mock 保存成功 |

### 8.2 RoomPage 到 Controller

| RoomPage 信号 | 目标 |
|---|---|
| `micToggleRequested` | `toggleMic` |
| `cameraToggleRequested` | `toggleCamera` |
| `shareToggleRequested` | `toggleShare` |
| `deafenToggleRequested` | `toggleDeafen` |
| `leaveRequested` | `leaveRoom` |
| `panelTabRequested` | `setPanelTab` |
| `stageFilterRequested` | `setStageFilter` |
| `stageLayoutRequested` | `setStageLayout` |
| `memberSelected` | `selectMember` |
| `memberFocusRequested` | `selectMember` + 强制 Focus 布局 |
| `memberActionRequested` | `memberAction` |
| `sourceRequested` | `setSource` |
| `volumeChanged` | `setVolume` |
| `deviceChanged(kind, id)` | 映射到麦克风/播放/摄像头 |
| `previewChanged` | `setPreview` |
| `muteAllRequested` | `muteAll` |
| `refreshDevicesRequested` | `refreshDevices` |
| `diagnosticsRequested` | `openDiagnostics` |
| `diagnosticsClosed` | `closeDiagnostics` |

`settingsRequested` 当前由 `RoomPage` 自己打开设置弹窗；该信号保留给后续埋点或业务设置接口。

### 8.3 Controller 到页面

| Controller 信号 | MainWindow 行为 |
|---|---|
| `joined` | 切换 RoomPage 并立即 `setState` |
| `left` | 切换回 PrejoinPage |
| `stateChanged` | 同步 Prejoin 预览状态和 RoomPage 状态 |
| `toastRequested` | 显示 Toast |
| `elapsedChanged` | 更新 RoomPage 计时 |
| `diagnosticsChanged` | 打开/关闭诊断，并隐藏/恢复窗口控制按钮 |

## 9. 页面实现说明

### 9.1 PrejoinPage

职责：

- 收集信令服务、用户、房间、Token
- 音频/视频后端和场景选择
- 麦克风/系统音源切换
- 设备下拉框与音量
- 摄像头 mock 预览
- 输出 `JoinOptions`

主要信号：

```text
joinRequested(JoinOptions)
saveSettingsRequested(JoinOptions)
cameraPreviewToggled(bool)
```

当前 `saveSettingsRequested` 只显示 Toast，不写配置。

### 9.2 RoomPage

房间页结构：

```text
RoomPage
  RoomMain
    RoomHeader
    RoomStage
      StageBar
      VideoGrid
    ControlBar
  RoomSidebar
    Tabs
    SideBody
    SideFooter
  DiagnosticsOverlay
```

主要行为：

- 入房后显示房间状态
- 成员/设备/状态 Tab 切换
- 侧栏滚动位置保存与恢复
- 共享时禁用宫格/聚焦切换
- 设置弹窗和离开确认
- 诊断抽屉右侧覆盖
- 窗口缩小时收起次要头部信息
- 宽度小于 1240 时隐藏控制栏中央提示

### 9.3 成员面板

- 单击成员：选择成员
- 双击成员：选择并切换聚焦布局
- 主持人可看到非本人成员的更多菜单：
  - 强制闭麦
  - 授予发言权
  - 移出房间
- 主持人可执行全员静音
- 非主持人自动隐藏管理入口

### 9.4 设备面板

- 麦克风和系统音源互斥
- 系统音模式下麦克风设备下拉框禁用
- 播放设备、摄像头、音量、预览
- 刷新设备和诊断入口
- 因为真实设备枚举尚未接入，下拉项由 `mockDevices()` 生成

### 9.5 状态面板

显示：

- 信令/连接状态
- 媒体模式、P2P 直连或 TURN 中继
- 房间模式和发言权
- 音频/视频编码
- 视频路数
- 加入时长
- 选中流分辨率、帧率、码率、RTT、抖动、丢包

当前数据来源为 `UiController::statsJson()` 的 mock JSON。

## 10. 视频布局和渲染边界

### 10.1 VideoGrid 布局规则

- 1 人：单画面
- 2 人且 Grid：远端大画面 + 右下角本地画中画
- 共享：共享主画面 + 底部全部成员胶片条
- Focus：选中成员主画面 + 底部其他成员胶片条
- Grid：最多 4 人 2 列，5 人及以上 3 列
- VideoOnly 过滤后为空时显示空状态

### 10.2 VideoSurface

当前稳定的帧接口：

```cpp
virtual void setFrame(const QImage &frame);
virtual void clearFrame();
```

`MockVideoSurface` 只绘制占位画面，不代表最终渲染实现。

### 10.3 推荐的首版真实视频接入

如果真实链路能够输出 CPU 可访问帧：

1. 解码得到 `QImage`
2. 切到 UI 线程
3. 调用 `VideoSurface::setFrame(frame)`
4. 保持 `VideoGrid` 和 `VideoTile` 不变

如果使用 QOpenGLWidget、D3D 或原生 HWND 渲染：

1. 不要直接把最终渲染器硬编码进 `VideoTile`
2. 给 `VideoTile` 注入一个渲染 Surface 工厂
3. 渲染 Surface 负责原生窗口句柄和 GPU 资源
4. `VideoTile` 只保留圆角边框、名称、状态图标和交互
5. 回调线程不能直接操作 QWidget

### 10.4 帧线程建议

```text
解码/媒体线程
  → 帧队列（限制长度，丢弃旧帧）
  → Qt queued signal / QMetaObject::invokeMethod
  → UI 线程 QImage 或原生 Surface 提交
```

禁止在解码线程直接调用 QWidget 方法。
禁止无限积压帧导致 UI 延迟持续增长。

## 11. 命令行与测试入口

### 11.1 场景

```powershell
--scenario prejoin
--scenario multi
--scenario one
--scenario sharing
--scenario devices
--scenario diagnostics
```

### 11.2 布局、过滤和尺寸

```powershell
--layout grid|focus
--filter all|video
--size 1440x900
--size 1100x720
```

### 11.3 面板与成员

```powershell
--panel members|devices|status
--selected me|u1|u2|...
```

### 11.4 mock 动作

```powershell
--action mic
--action camera
--action share
--action deafen
--action muteall
--action source-system
--action source-mic
```

`--action` 可以重复多次。

### 11.5 弹窗和诊断

```powershell
--show-settings
--show-leave
--scenario diagnostics
```

### 11.6 截图与冒烟

```powershell
--screenshot build\shots\test.png
--smoke-seconds 2
```

截图模式会自动创建父目录并退出。模态弹窗截图使用屏幕区域抓取，以同时包含主窗口和弹窗。

## 12. 窗口和主题

### 12.1 FramelessWindow

- 使用原生 `WM_NCHITTEST`
- 支持拖动、八方向缩放
- 支持最小化和最大化
- 顶部标题区域不使用子控件时可拖动
- 控件如按钮、输入框、下拉框、滚动区域会阻止窗口拖动
- 需要阻止拖动的自定义区域可设置动态属性：

```cpp
widget->setProperty("noWindowDrag", true);
```

### 12.2 Theme

- 图标由 `Theme::drawIcon()` 用 `QPainter` 绘制
- 不依赖外部 SVG 图标运行时
- 下拉箭头使用 qrc 中的小型 PNG
- 全局样式位于 `resources/theme.qss`

### 12.3 自定义控件

- `Pill`：自绘状态胶囊，支持 accent/ok/warn/danger/step/stepActive
- `SegmentedControl`：按钮组封装，样式由全局 QSS 控制
- `ControlButton`：图标 + 文案
- `IconButton`：自绘圆形按钮
- `Toast`：主窗口底部提示，自动消失

## 13. CoreClient/WebRTC 接入方案

### 13.1 推荐架构

```text
┌──────────────────────────────────────────────┐
│                 Qt UI 主线程                  │
│                                              │
│  MainWindow → PrejoinPage / RoomPage          │
│       │                                      │
│       └── UiController（UI 门面）             │
│               │                              │
│               └── CoreUiAdapter               │
└───────────────────┬──────────────────────────┘
                    │ queued signals/slots
┌───────────────────▼──────────────────────────┐
│               CoreClient/WebRTC              │
│  - 信令和房间生命周期                         │
│  - PeerConnection / Transport                 │
│  - 音视频采集、编解码                         │
│  - 设备枚举                                   │
│  - getStats                                   │
└──────────────────────────────────────────────┘
```

推荐原则：

- 页面层只认识 `UiState` 和 `UiController`
- CoreClient 只通过适配层输出状态和事件
- UI 线程不阻塞等待网络、设备或 getStats
- 媒体线程不直接持有或操作 QWidget

### 13.2 最小侵入接入路径

建议保留当前 `UiController` 类名和 public slots/signals，内部增加后端注入。

可选实现：

```cpp
class IUiBackend {
public:
    virtual ~IUiBackend() = default;
    virtual void join(const JoinOptions &options) = 0;
    virtual void leaveRoom() = 0;
    virtual void setMicEnabled(bool enabled) = 0;
    virtual void setCameraEnabled(bool enabled) = 0;
    virtual void setScreenShareEnabled(bool enabled) = 0;
    virtual void setSource(const QString &source) = 0;
    virtual void setDeafen(bool enabled) = 0;
    virtual void setDevice(DeviceKind kind, const QString &id) = 0;
    virtual void requestStats() = 0;
};
```

`CoreUiAdapter` 负责：

1. 把 UI 命令转为 CoreClient 调用
2. 把 CoreClient 回调转为 Qt 信号
3. 把业务错误转为 `toastRequested`
4. 把成员和连接变化写入 `UiState`
5. 处理重连、断线和离房清理

### 13.3 第一批建议替换的接口

按以下顺序替换，便于逐步联调：

1. `join()` / `leaveRoom()`
2. 成员列表和连接状态
3. `toggleMic()` / `toggleCamera()`
4. `toggleShare()`
5. 设备枚举和设备切换
6. `toggleDeafen()`
7. 真实视频帧
8. `statsJson()` / 诊断
9. 设置持久化

### 13.4 入房接入检查清单

- 信令连接成功
- 鉴权和 token 错误有明确提示
- 房间不存在、满员、重复入房有明确提示
- 本地媒体初始化失败能回退到音频模式
- 入房成功后再显示 RoomPage
- 离房时停止采集、释放 PeerConnection、停止计时
- 离房后清空成员和选中流
- UI 不保留已删除成员的原生渲染对象

### 13.5 成员状态映射

CoreClient 成员变化需要映射：

```text
MemberInfo.id       ← 稳定用户/参与者 ID
MemberInfo.name     ← 展示名
MemberInfo.role     ← host/member
MemberInfo.mic      ← 实际音轨状态或服务端状态
MemberInfo.camera   ← 视频轨状态
MemberInfo.poor     ← 网络质量策略结果
MemberInfo.speaking ← 音量检测或服务端 speaking 事件
MemberInfo.isSelf   ← SDK 的本地参与者判断
MemberInfo.source   ← mic/system/screen
```

不要用显示名作为唯一 ID。成员排序应保持稳定，避免每次刷新导致 UI 卡片跳动。

## 14. 真实视频实现细则

### 14.1 CPU 帧路径

适合软解、测试或首版接入：

```text
RTP → 解码 → YUV → QImage → VideoSurface::setFrame
```

注意：

- 保持原始宽高比
- 不要在高频回调里做昂贵缩放
- `QImage` 跨线程传递应确保内存生命周期
- 建议使用 `QImage::Format_RGB32` 或 `ARGB32`
- 可使用共享帧池或帧引用计数减少拷贝

### 14.2 原生 D3D/OpenGL 路径

Room 中 `openGl`/D3D 渲染窗口的参考工程位于：

```text
D:\work\device_tool\omni_station\ui_module\video_render
```

推荐复用思路：

- `VideoTile` 保留边框、名字、麦克风/摄像头状态、主持人皇冠
- 新增 `NativeVideoSurface` 作为子控件或原生子窗口
- 将 `winId()`/HWND 交给渲染器
- 设置正确的 `WA_NativeWindow`、`WA_NoSystemBackground` 等窗口属性
- 确保重叠的 QLabel 始终位于视频 Surface 上方

### 14.3 Surface 注入建议

当前 `VideoTile` 内部直接创建 `MockVideoSurface`，接真实业务时建议改为工厂注入：

```cpp
using VideoSurfaceFactory = std::function<VideoSurface *(QWidget *)>;
```

由 `MainWindow` 或 `RoomPage` 创建工厂，传入 `VideoGrid`，再传给每个 `VideoTile`。
这样 mock 和真实渲染可以并存，也方便截图测试。

### 14.4 帧丢弃和背压

建议策略：

- 每个 Surface 只保留最新 1–2 帧
- UI 线程处理不过来时丢弃旧帧
- 不要在主线程做深拷贝大分辨率帧
- 共享屏幕和摄像头可配置不同分辨率
- 窗口最小化或隐藏时暂停/降低渲染频率

## 15. 设备和设置接入

### 15.1 设备枚举

当前 mock 设备在：

```text
RoomPage.cpp / mockDevices()
```

真实接入后应替换为 CoreClient/系统设备 API，并提供稳定 ID：

- 麦克风：WASAPI capture device ID
- 播放设备：WASAPI render device ID
- 系统音：WASAPI loopback
- 摄像头：设备 moniker / symbolic link

### 15.2 切换规则

- 麦克风设备切换：建议下次开麦生效，或明确提示短时断流
- 播放设备切换：需要重建音频输出链路
- 摄像头切换：需要停止旧采集、启动新采集
- 系统音模式：麦克风设备下拉框禁用
- 设备拔插：刷新后保留仍然存在的当前选择

### 15.3 设置持久化

需要持久化的字段建议：

- server
- userId（视业务需求）
- roomId
- audioBackend
- modePref
- talkMode
- source
- volume
- micDevice / playoutDevice / cameraDevice

Token 不建议明文落盘。`fetchDevToken` 只在开发模式保留。

## 16. getStats 和诊断接入

### 16.1 当前实现

`UiController::statsJson()` 每次从 mock `UiState` 生成 JSON：

```text
room
self
selected_stream
```

状态面板读取 `selected_stream` 的分辨率、帧率、码率、RTT、抖动和丢包。

### 16.2 真实接入建议

当前 `statsJson()` 是同步接口，不适合直接调用真实 `getStats()`。

推荐改为：

```text
MainWindow 请求统计
  → UiController::requestStats()
  → CoreClient 异步 getStats
  → statsJsonChanged(QString)
  → RoomPage::setStatsJson()
```

这样可以避免 UI 卡顿。

### 16.3 诊断日志

建议构建固定容量环形缓冲区：

- 信令事件
- ICE/DTLS 状态
- 音视频轨道状态
- 设备切换
- 错误码和重连次数
- 选中流统计

不要在日志中写入 token、密码或完整 SDP 敏感内容。

## 17. 线程和生命周期约束

### 17.1 线程规则

- 所有 QWidget 操作必须在 Qt UI 线程
- CoreClient 回调通常来自网络/媒体线程，必须通过 queued connection 转发
- `QImage` 或原生 frame handle 的生命周期必须明确
- `getStats` 不得在 UI 线程同步等待
- 设备枚举不得长时间阻塞 UI

### 17.2 对象生命周期

- `VideoTile` 删除前必须解除对原生渲染对象的引用
- 离房时必须停止所有 Surface 的帧提交
- 成员移除时 `VideoGrid` 会 `deleteLater()` 对应 Tile
- 真实渲染器应监听 Tile/Surface 的销毁信号并释放 GPU/HWND 资源
- 不要在媒体线程保存可能已销毁的 QWidget 指针

### 17.3 计时器

- `UiController` 内部使用 `QTimer` 计时
- 离房停止计时
- 业务真实计时可由 CoreClient 提供，也可保留 UI 计时器

## 18. 错误状态和用户提示

建议将业务错误映射到 UI：

| 场景 | UI 行为 |
|---|---|
| 信令连接失败 | 入房页错误提示，不进入房间 |
| 鉴权失败 | Toast/表单错误，不重试 |
| 房间满员 | 入房页提示 |
| 媒体设备初始化失败 | 进入音频模式或提示用户 |
| 设备被占用 | 提示并保留原设备 |
| 屏幕共享取消 | 恢复原布局，不显示共享状态 |
| 网络重连 | 头部连接状态变为重连中 |
| 对端断开 | 移除或标记成员离线 |
| getStats 失败 | 状态页显示“暂不可用”，不阻塞 UI |

当前 `toastRequested` 的 tone 支持 `info`、`ok`、`warn`、`error`。

## 19. 与现有 client-win 工程集成

### 19.1 推荐构建方式

不要直接把这个原型的所有源文件 glob 进主业务工程。

建议：

1. 把 UI 原型抽成静态库或独立 target
2. 由主工程链接该 target
3. 资源 qrc 也放入 UI target
4. 业务适配器放在主工程业务模块
5. 通过配置或编译宏切换 mock 与真实后端

示例目标结构：

```text
rtc_talk_ui
  - pages
  - widgets
  - video UI
  - resources

rtc_talk_app
  - CoreUiAdapter
  - CoreClient
  - signaling
  - media
  - platform integration
```

### 19.2 平台依赖

当前 Windows 依赖：

- `user32`
- `dwmapi`
- Qt5::Core / Gui / Widgets

如果切到真实 D3D 渲染，再按实际实现增加 D3D11、DXGI 或 OpenGL 依赖。

### 19.3 兼容性

- 源码使用 C++17
- 中文源文件要求 `/utf-8`
- Qt 5.15 使用 `AA_EnableHighDpiScaling`
- 不要依赖 Qt 私有 API
- 无边框窗口相关逻辑仅 Windows 生效

## 20. 测试基线和验收结果

### 20.1 六场景回归

| 场景 | 结果 |
|---|---|
| prejoin | 退出码 0 |
| multi | 退出码 0 |
| one | 退出码 0 |
| sharing | 退出码 0 |
| devices | 退出码 0 |
| diagnostics | 退出码 0 |

### 20.2 尺寸和缩放

| 项 | 结果 |
|---|---|
| 1100×720 | 通过 |
| 125% Qt Scale | 通过 |
| 150% Qt Scale | 通过 |

### 20.3 冒烟

| 场景 | 结果 |
|---|---|
| multi 2 秒 | 退出码 0 |
| diagnostics 2 秒 | 退出码 0 |

### 20.4 截图索引

```text
build/shots/m6-prejoin.png
build/shots/m6-multi.png
build/shots/m6-one.png
build/shots/m6-sharing.png
build/shots/m6-devices.png
build/shots/m6-diagnostics.png
build/shots/m6-minimum-1100.png
build/shots/m6-scale-125.png
build/shots/m6-scale-150.png
build/shots/m5-settings.png
build/shots/m5-leave.png
```

### 20.5 手工测试清单

- 入房和离房
- 成员选择、双击聚焦
- 主持人菜单
- 全员静音
- 麦克风/摄像头状态
- 屏幕共享和停止共享
- 静音收听
- 设备源切换
- 拖动音量不中断
- 设置弹窗状态同步
- 诊断遮罩关闭和关闭按钮
- 最小化、最大化、缩放、拖动
- 1100×720 无遮挡
- 125%/150% 下文字和控件不裁切

## 21. 已知限制和风险

### 21.1 功能限制

- 所有业务、成员、设备、统计均为 mock
- 设置不持久化
- 诊断不导出
- 模态弹窗固定居中，不支持拖动
- 当前 mock 成员数为 6，胶片条未实现横向滚动
- 尚无真实重连、断线、设备热插拔流程

### 21.2 技术风险

- `VideoSurface` 当前是 QWidget + QImage 边界，原生 GPU 渲染需要适配或注入
- `VideoTile` 内部直接 new MockVideoSurface，接真实渲染前建议增加工厂
- `statsJson()` 为同步 mock 接口，真实 getStats 应改成异步
- `UiController` 是具体类，MainWindow 直接持有；如果替换后端，建议保留门面接口
- FramelessWindow 使用 Windows 原生消息，需在目标 Windows 版本和 DPI 上回归
- 大量成员时需要重新评估 Grid 和 Filmstrip 的布局策略
- 未建立正式自动化 UI 测试框架，目前以截图和冒烟为主

## 22. 后续推荐里程碑

### 阶段 A：CoreClient 最小接入

- 替换 join/leave
- 接入成员列表和连接状态
- 接入麦克风/摄像头开关
- 保留界面和 mock fallback

### 阶段 B：真实媒体

- 接入本地/远端视频帧
- 根据帧格式选择 QImage 或原生渲染
- 处理共享屏幕比例和成员缩略图
- 完成线程和资源释放

### 阶段 C：设备和统计

- 真实设备枚举
- 设备热插拔
- 异步 getStats
- 诊断日志接入

### 阶段 D：产品化

- 设置持久化
- 错误码和重连 UI
- 多显示器/DPI 测试
- 可访问性和键盘操作
- 安装包、升级和崩溃日志

## 23. 变更时应优先检查的文件

| 变更 | 文件 |
|---|---|
| 入房参数 | `PrejoinPage.*`、`UiState.h`、`UiController.*` |
| 页面切换 | `MainWindow.*` |
| 房间布局 | `RoomPage.*` |
| 成员/设备/状态 | `RoomPage.*`、`UiState.h` |
| 视频布局 | `VideoGrid.*`、`VideoTile.*` |
| 真实帧 | `VideoSurface.*`、`VideoTile.*` |
| 主题 | `Theme.*`、`theme.qss` |
| 窗口行为 | `FramelessWindow.*`、`MainWindow.*` |
| 场景/截图 | `main.cpp` |

## 24. 交接完成标准

后续接入业务时，建议以以下条件作为第一轮完成标准：

- UI 代码不直接包含 CoreClient/WebRTC 头文件
- 入房、离房、成员变化、媒体开关可真实工作
- 视频帧不会在 UI 线程阻塞
- 离房后媒体资源和渲染对象全部释放
- 设备切换和错误状态有用户提示
- 诊断数据来自真实 Stats 或明确标记不可用
- 六场景截图仍可生成
- 1100×720 和 125%/150% 缩放不回归
- 无持续增长的帧队列、日志和内存占用

---

如后续接入涉及新的业务模块，建议在本文件末尾追加“附录：业务差异与接入记录”，不要修改历史验收结果。
