# rtc-talk-win 详细架构设计

> 状态：待评审。未确认前不开始编码、不提交。  
> 日期：2026-10-26  
> 需求基线：`docs/requirements.md`  
> 工程目录：`D:\google\webrtc-win-client\rtc-talk-win`

## 1. 设计目标

本项目按生产桌面客户端设计，架构首先解决以下问题：

1. UI、业务、媒体、协议、Windows 设备能力之间没有隐藏耦合。
2. WebRTC 和 Media Foundation 被限制在基础设施层，不能污染业务与 UI。
3. 摄像头预览、摄像头发布、屏幕共享、远端订阅各自拥有独立生命周期。
4. 协议事件、媒体线程和 UI 线程之间有明确的所有权和并发模型。
5. 所有关键模块可接 Fake 实现做确定性测试。
6. 任何演示代码只能作为行为参考，不能成为生产模块的内部结构。

## 2. 架构决策摘要

| 决策 | 内容 |
|---|---|
| A1 | `rtc-talk-win` 作为独立 Git 仓库，初始分支 `rtc-talk-win` |
| A2 | Windows 10/11 x64，Qt 5.15.2 Widgets，MSVC 2022 |
| A3 | UI/应用层为 `/MD`；媒体引擎为 `/MT` DLL；跨界只使用 C ABI |
| A4 | JSON 只允许存在于信令协议层和 C ABI 边界 |
| A5 | 领域层为纯 C++，不依赖 Qt、WebRTC、Windows SDK、JSON |
| A6 | WebRTC 作为媒体传输适配器，不作为业务模型 |
| A7 | 首版视频显示使用 QImage Surface，接口允许后续替换 D3D/OpenGL |
| A8 | 正式环境使用 WSS；`ws://` 仅限开发环境 |

以上决策是当前设计前提。A1 的仓库形态和 A2 的 Qt 版本需要在评审时明确确认。

## 3. 系统上下文

```text
┌───────────────┐        WSS/JSON         ┌──────────────────┐
│ rtc-talk.exe  │◄──────────────────────►│ talk signaling   │
│               │                         │ server           │
│ Qt UI         │                         └────────┬─────────┘
│ Business      │                                  │
│ C ABI gateway │                                  │ SFU/P2P
└───────┬───────┘                                  │
        │ versioned C ABI                          │
┌───────▼───────────────────────┐                  │
│ rtcwin_engine.dll             │                  │
│ signaling/session/media core  │◄─────────────────┘
│ libwebrtc + Win devices       │
└───────┬───────────────┬───────┘
        │               │
┌───────▼───────┐ ┌─────▼─────────┐
│ Media         │ │ Windows       │
│ Foundation    │ │ WASAPI/WGC    │
└───────────────┘ └───────────────┘
```

### 3.1 进程与 DLL 边界

```text
rtc-talk.exe        /MD  Qt UI + application services + client gateway
rtcwin_engine.dll   /MT  session engine + media orchestration + adapters
log-runtime.dll     /MT  日志运行时，可由引擎与诊断工具使用
```

UI 进程不包含 `webrtc.h`、asio 私有头、Media Foundation 采集实现或协议 JSON
解析代码。这样可避免：

- UI 和 libwebrtc CRT 不一致。
- 页面直接依赖媒体生命周期。
- 协议与界面互相绑死。
- 后续替换传输或渲染时影响业务层。

## 4. 分层设计

```text
┌─────────────────────────────────────────────────────────────┐
│ Presentation                                                │
│ MainWindow · PrejoinPage · RoomPage · Dialogs · VideoGrid   │
└─────────────────────────────┬───────────────────────────────┘
                              │ ViewModel commands/state
┌─────────────────────────────▼───────────────────────────────┐
│ Application                                                 │
│ SessionCoordinator · PrejoinCoordinator · DeviceCoordinator │
│ DiagnosticsCoordinator · SettingsService · State reducers   │
└─────────────────────────────┬───────────────────────────────┘
                              │ strong typed domain ports
┌─────────────────────────────▼───────────────────────────────┐
│ Domain                                                      │
│ Session · Room · Member · Floor · Policy · Stream · Device  │
│ Error · Result · typed events · port interfaces             │
└─────────────────────────────┬───────────────────────────────┘
                              │ implementations
┌─────────────────────────────▼───────────────────────────────┐
│ Infrastructure                                              │
│ In-process client gateway · engine adapters · Win platform  │
└─────────────────────────────┬───────────────────────────────┘
                              │ C ABI
┌─────────────────────────────▼───────────────────────────────┐
│ Engine                                                      │
│ RPC · SessionEngine · MediaCoordinator · WebRTC adapters    │
└─────────────────────────────────────────────────────────────┘
```

### 4.1 依赖规则

允许：

```text
presentation -> application -> domain
application -> domain
infrastructure -> domain
bootstrap -> presentation + application + infrastructure
engine -> protocol + media ports + WebRTC/Windows adapters
```

禁止：

```text
domain -> Qt / WebRTC / Windows / JSON
application -> QWidget / PeerConnection / Media Foundation
presentation -> WebRTC / asio / protocol JSON
adapter A -> adapter B 的具体实现
```

依赖规则由脚本在构建前检查：

- `libs/domain` 不得出现 `Q`, `webrtc/`, `asio`, `nlohmann`, `windows.h`。
- `libs/application` 不得 include `QWidget`、`webrtc/`、`mf*.h`。
- `libs/ui` 不得 include `webrtc/`、`asio`、`nlohmann`。
- UI target 的链接依赖只能出现 `rtcwin_application`、`rtcwin_ui_*` 和 Qt。

## 5. 模块设计

### 5.1 Domain

职责：

- 定义业务状态和状态转换规则。
- 定义应用层需要依赖的端口。
- 不包含 I/O、线程、Qt 对象或平台 API。

核心类型：

```cpp
struct SessionId;
struct RoomId;
struct UserId;
struct TrackMid;

enum class ConnectionPhase;
enum class RoomRole;
enum class TalkMode;
enum class VideoSource;
enum class CaptureState;
enum class PublishState;

struct PeerState;
struct VideoTrackState;
struct FloorState;
struct RoomPolicy;
struct SessionSnapshot;
struct DeviceInfo;
struct MediaStats;
struct AppError;
```

原则：

- 使用强类型 ID，减少字符串误用。
- 状态快照不可变；由 reducer 生成新快照。
- 错误使用稳定错误码和分类，不只传一个字符串。
- 领域事件是 `std::variant` 或强类型结构，不直接传 JSON。

核心事件：

```text
ConnectionChanged
SessionReady
RoomJoined
RoomSnapshotReceived
PeerJoined/Updated/Left
FloorChanged
PolicyChanged
VideoTrackPublished/Unpublished
ProducerStateChanged
AudioLevelsChanged
CommandCompleted/CommandFailed/TimedOut
MediaFaulted
```

### 5.2 Protocol

职责：

- 实现 `talk.v1.json` 信封、字段、错误码和消息校验。
- 将协议 JSON 转为领域事件。
- 将领域命令转为协议 JSON。
- 处理未知字段、非法长度、重复 ID 和消息上限。

不负责：

- 连接生命周期。
- 重连状态机。
- 媒体对象。
- UI 状态。

协议层入口：

```cpp
class IProtocolCodec {
 public:
  virtual ParseResult Parse(std::string_view text) const = 0;
  virtual std::string EncodeRequest(const Request& request) const = 0;
  virtual std::string EncodeNotification(const Notification&) const = 0;
  virtual std::string EncodeResponse(const Response& response) const = 0;
};
```

### 5.3 Signaling

拆分三个对象：

```text
SignalingTransport
  - WebSocket/TLS 连接
  - 收发字节、关闭、错误

RpcClient
  - 请求 ID
  - 超时、取消、响应关联
  - 双向请求分发

ReconnectPolicy
  - 指数退避
  - resume 窗口
  - 主动离房/顶号不再重连
```

`RpcClient` 不包含房间逻辑。即使 Server 发来 `sfu.offer`，也只交给上层注册的
请求处理器。

传输接口：

```cpp
class ISignalingTransport {
 public:
  virtual ~ISignalingTransport() = default;
  virtual void Connect(const Endpoint&, const Token&,
                       ITransportObserver&) = 0;
  virtual void Send(std::string message) = 0;
  virtual void Close(CloseReason) = 0;
};
```

生产要求：

- WSS 使用系统证书链校验。
- 禁止关闭主机名验证。
- 连接、握手、心跳和关闭都有独立超时。
- 日志不输出完整 Token。

### 5.4 SessionEngine

职责：

- 持有会话状态机。
- 协调信令、房间、发言权和媒体。
- 对外提供命令，对内分发领域事件。
- 维护 generation，隔离旧回调。

内部子状态机：

```text
SessionLifecycle
  Disconnected -> Connecting -> Handshaking -> Ready
  Ready -> Joining -> InRoom
  InRoom -> Leaving -> Ready
  Any -> Reconnecting -> Ready | Failed

RoomState
  Empty -> Joining -> Active -> Leaving -> Empty

FloorState
  Free | Idle | Requesting | Queued | Granted | Releasing | Revoked

MediaMode
  Idle | SfuPublishing | SfuSubscribed | P2pNegotiating | P2pConnected
```

拆分职责：

```text
SessionEngine
  ├─ RoomController
  ├─ FloorController
  ├─ HostController
  ├─ MediaModeCoordinator
  ├─ DeviceCoordinator
  └─ DiagnosticsCoordinator
```

禁止重新形成单文件 `CallSession`：

- 每个控制器有独立接口和测试。
- 协议命令先进入 `CommandRouter`，再分派给控制器。
- `CommandRouter` 不做业务决策。

### 5.5 Camera

```text
ICameraService
  EnumerateDevices()
  Open(DeviceId)
  SubscribeHotplug()

ICameraCaptureSession
  Start()
  Stop()
  AddFrameSink()
  RemoveFrameSink()
  State()
```

Windows adapter：

- 枚举使用 Media Foundation。
- 打开/采集在独立 COM MTA 采集线程。
- 对外输出统一的 `VideoFrameView`。
- 权限、占用、拔插和设备格式错误映射为 `DeviceError`。

CameraService 不持有 Session、Producer 或 PeerConnection。

### 5.6 Audio

```text
IAudioService
  EnumerateRecordingDevices()
  EnumeratePlayoutDevices()
  SelectRecordingDevice()
  SelectPlayoutDevice()
  SetSource(Microphone|SystemLoopback)
  SetVolume()
  SetMuted()
```

音频设备选择与 WebRTC ADM 的绑定在 `audio_win` adapter 内完成，不进入 UI。

### 5.7 Screen capture

```text
IScreenService
  EnumerateSources()
  Start(SourceId, CaptureProfile)
  Stop()
```

首版可选择主屏，但接口保留 SourceId，后续支持多屏不需要修改 Session API。

### 5.8 MediaCoordinator

职责：

- 协调摄像头、音频、屏幕、PeerConnection。
- 管理 SFU/P2P 模式切换。
- 管理 Producer 暂停和恢复。
- 管理本地预览与正式发布的引用关系。
- 生成媒体统计和故障事件。

不负责：

- Token、房间成员、发言权。
- UI 文案。
- 设备列表页面。

#### 5.8.1 三条视频管线

```text
Local preview:
  CameraCapture -> PreviewSession -> LocalFrameSink

Publish:
  CameraCapture -> WebRtcVideoTrack -> Sender
  ScreenCapture -> WebRtcVideoTrack -> Sender

Remote subscribe:
  Receiver -> Decoder -> RemoteVideoSink -> FrameBridge -> UI
```

预览和发布可以共享同一个 `CameraCapture`，但生命周期通过引用计数控制：

```text
Acquire(CaptureConsumer::Preview)
Acquire(CaptureConsumer::Publisher)
Release(...)
```

只有最后一个消费者释放后才停止采集。这样停止预览不会误停发布，停止发布也不会
无条件关闭用户仍在查看的预览。

#### 5.8.2 发布状态机

```text
Idle
  -> Starting
  -> Negotiating
  -> Publishing
  -> Paused
  -> Stopping
  -> Idle
```

失败路径：

```text
Starting/Negotiating/Publishing
  -> Reverting
  -> Idle
  + MediaFaulted event
```

### 5.9 Remote video registry

远端视频按 `mid` 管理：

```cpp
struct VideoStream {
  TrackMid mid;
  UserId uid;
  VideoSource source;
  bool producer_paused;
  int64_t last_frame_ms;
  bool receiving;
};
```

规则：

- 每流只保留最新一帧。
- UI 刷新合并窗口约 33ms。
- producer paused 立即清空画面。
- 5 秒无帧将 Surface 置为空闲。
- 帧回调线程与 UI 线程之间只传已拷贝帧。
- 流元信息与帧数据分开更新，避免每帧重建 UI。

### 5.10 Application

应用层将引擎事件转为 UI 可消费状态，不包含 WebRTC 或 QWidget。

```text
SessionCoordinator
  connect/join/leave/reconnect intent

DeviceCoordinator
  device enumeration/selection/preview intent

SettingsService
  non-sensitive settings persistence

CredentialService
  DPAPI/Credential Manager

DiagnosticsService
  stats/log/export
```

应用层输出不可变快照：

```cpp
struct PrejoinViewModelState;
struct RoomViewModelState;
struct DeviceViewModelState;
struct DiagnosticsViewModelState;
```

页面只订阅这些状态，不直接解析引擎事件。

## 6. UI 架构

### 6.1 组件层次

```text
presentation/
  shell/
    AppWindow
    WindowControls
    ToastHost
  pages/
    PrejoinPage
    RoomPage
  components/
    MemberListModel
    MemberRow
    ControlBar
    DevicePanel
    StatusPanel
    PolicyDialog
    DiagnosticsDrawer
  video/
    VideoSurface
    ImageVideoSurface
    VideoSurfaceFactory
    VideoTile
    VideoLayoutEngine
  theme/
```

### 6.2 数据绑定

- 成员列表使用 `QAbstractListModel`，不再每次事件重建全部 QWidget。
- 房间页只接收 `RoomViewModelState`。
- 视频区只接收 `VideoStreamState` 和帧事件。
- 选中成员、布局和过滤属于 UI 会话状态，不写入协议模型。
- 页面不持有 `talk_handle`、PeerConnection 或 JSON 文档。

### 6.3 视频渲染边界

```cpp
class VideoSurface : public QWidget {
 public:
  virtual void Present(const VideoFrame&) = 0;
  virtual void Clear() = 0;
};
```

首版：

```text
ImageVideoSurface : QWidget + QImage
```

后续：

```text
D3D11VideoSurface : QWidget / native child window
OpenGLVideoSurface : QOpenGLWidget
```

`VideoTile` 和布局代码只依赖 `VideoSurface` 接口。

### 6.4 布局引擎

`VideoLayoutEngine` 是纯计算模块，不创建 QWidget：

```cpp
LayoutResult Calculate(const LayoutInput&);
```

输入成员、视频流、选择、共享、布局和过滤条件；输出每个 tile 的 rect、显示状态、
是否画中画、Surface 类型。这样布局可以在无窗口环境中单元测试。

## 7. C ABI 设计

### 7.1 目标

- UI 不接触 libwebrtc。
- 引擎可独立升级。
- 结构体带 `struct_size` 和 `abi_version`。
- 旧字段不重排，新能力只追加。
- 回调生命周期可验证。

### 7.2 头文件草案

```c
#define RTCWIN_ABI_VERSION 1u

typedef struct rtcwin_handle rtcwin_handle;

typedef struct rtcwin_frame_view {
  const char* mid;
  const uint8_t* bgra;
  int32_t width;
  int32_t height;
  int32_t stride;
  int64_t timestamp_us;
} rtcwin_frame_view;

typedef void (*rtcwin_event_fn)(void* userdata,
                                const char* method,
                                const char* json);

typedef void (*rtcwin_frame_fn)(void* userdata,
                                const rtcwin_frame_view* frame);

typedef struct rtcwin_options {
  uint32_t struct_size;
  uint32_t abi_version;
  void* userdata;
  rtcwin_event_fn on_event;
  rtcwin_frame_fn on_remote_frame;
  rtcwin_frame_fn on_local_frame;
} rtcwin_options;

rtcwin_handle* rtcwin_create(const rtcwin_options* options);
void rtcwin_destroy(rtcwin_handle* handle);

uint64_t rtcwin_submit(rtcwin_handle* handle,
                       const char* method,
                       const char* json,
                       int32_t timeout_ms);
```

### 7.3 命令/事件边界

- C ABI 的 method/json 是唯一允许的 JSON 边界。
- `ClientGateway` 将 JSON 映射成强类型应用事件。
- UI 页面永远看不到 JSON。
- Engine 内部命令解析后立即转为强类型 `Command`。
- 命令 ID 由 C ABI 层生成，结果带同一 ID。

### 7.4 回调规则

- Event 回调在引擎控制线程。
- Remote/local frame 回调在媒体或采集线程。
- 回调期间指针只在调用栈内有效。
- 回调禁止阻塞和销毁句柄。
- 引擎关闭前先停止回调，再等待媒体线程退出。

## 8. 线程模型

| 线程 | 所属 | 职责 |
|---|---|---|
| UI thread | `rtc-talk.exe` | QWidget、模型、Surface 绘制 |
| Client gateway thread | UI 进程 | C ABI 事件队列、帧合并 |
| Engine control thread | `rtcwin_engine.dll` | 信令、状态机、命令路由 |
| asio I/O | Engine | WebSocket、定时器 |
| Media task thread | Engine | 串行媒体设备操作 |
| WebRTC network | libwebrtc | ICE/RTP |
| WebRTC worker | libwebrtc | 编解码、媒体处理 |
| WebRTC signaling | libwebrtc | PeerConnection API |
| Camera capture | Windows adapter | COM MTA、摄像头帧 |
| Desktop capture | Windows adapter | WGC/DXGI 帧 |

规则：

- 不使用线程间共享可变 UI 对象。
- 引擎对 UI 只发值类型事件。
- 命令队列有上限；满时明确返回 overload。
- 帧队列每流上限 1。
- 所有线程退出均有超时和日志。

## 9. 生命周期与关闭顺序

### 9.1 应用启动

```text
main
  -> 安装崩溃处理
  -> 初始化日志
  -> 加载非敏感配置
  -> 初始化本地化/主题
  -> 创建 ClientGateway
  -> 创建 MainWindow
  -> 异步枚举设备
  -> show
```

### 9.2 入房

```text
UI 提交 Connect
  -> Gateway 生成 command id
  -> Engine Connect
  -> hello
  -> Ready
  -> RoomJoin
  -> RoomSnapshot
  -> 创建媒体管线
  -> UI RoomState
```

### 9.3 离房

```text
停止 UI 新命令
  -> 停止预览/发布
  -> room.leave
  -> 停止信令
  -> 销毁媒体实例
  -> 清空房间快照
  -> 返回 Prejoin
```

### 9.4 应用退出

```text
关闭窗口
  -> 拒绝新命令
  -> 停止采集/预览/发布
  -> 发送 session.bye（若可能）
  -> 关闭信令
  -> 等待引擎线程
  -> 销毁 Surface/ViewModel
  -> 关闭日志
```

所有关闭步骤必须幂等，可重复调用。

## 10. 错误模型

```cpp
enum class ErrorDomain {
  Validation,
  Authentication,
  Network,
  Protocol,
  Permission,
  Device,
  Media,
  Session,
  Storage,
  Internal,
};

struct AppError {
  ErrorDomain domain;
  int code;
  bool retryable;
  std::string operation;
  std::string user_message_key;
  std::string technical_detail;
  std::string correlation_id;
};
```

规则：

- UI 只根据 `user_message_key` 和参数生成文案。
- 技术细节可写入脱敏日志，不直接展示完整底层错误。
- 每个失败命令必须有回滚或明确状态收敛。
- 设备错误区分权限、占用、拔插、格式不支持和内部失败。

## 11. 观测与安全

### 11.1 日志

- 结构化字段：时间、级别、模块、线程、command_id、session_id、room_id、错误码。
- 日志分级：Trace/Debug/Info/Warn/Error。
- 默认 Info，排障时动态调整。
- 滚动大小和数量有上限。

### 11.2 脱敏

禁止写日志：

- Token、密码、Authorization 头。
- 完整 SDP、ICE candidate 中的敏感地址。
- 用户输入的房间密码。
- 未脱敏的设备序列号。

### 11.3 诊断包

包含：

- 版本、OS、CPU、GPU 名称。
- 服务端提交号。
- 脱敏日志。
- 会话状态快照。
- 媒体统计摘要。
- 设备能力摘要。

不包含：

- Token。
- 完整 SDP/候选地址。
- 摄像头或屏幕截图，除非用户主动导出。

## 12. 仓库与 CMake 结构

```text
rtc-talk-win/
  CMakeLists.txt
  CMakePresets.json
  vcpkg.json
  cmake/
    dependencies.cmake
    webrtc.cmake
    warnings.cmake
    packaging.cmake
  apps/rtc-talk/
  libs/
    domain/
    protocol/
    application/
    platform_win/
    audio_win/
    camera_win/
    screen_win/
    webrtc_media/
    engine/
    client_api/
    ui/shell/
    ui/components/
    ui/pages/
    ui/video/
    ui/theme/
  tests/
    unit/
    integration/
    smoke/
  tools/
  packaging/windows/
  scripts/
  docs/
  plan/
```

### 12.1 CMake 目标依赖

```text
rtcwin_domain                 纯领域类型与端口，无第三方依赖
  │
  ├── rtcwin_protocol         -> rtcwin_domain
  ├── rtcwin_media_ports      -> rtcwin_domain
  └── rtcwin_platform_win     -> rtcwin_domain

rtcwin_camera_win             -> rtcwin_media_ports + rtcwin_platform_win
rtcwin_audio_win              -> rtcwin_media_ports + rtcwin_platform_win
rtcwin_screen_win             -> rtcwin_media_ports + rtcwin_platform_win
rtcwin_webrtc_media           -> rtcwin_media_ports + rtcwin_platform_win

rtcwin_engine                 -> rtcwin_domain + rtcwin_protocol
                                  + 四个基础设施 adapter
rtcwin_client_api             -> rtcwin_engine（仅导出 C ABI）

rtcwin_application            -> rtcwin_domain
rtcwin_client_gateway         -> rtcwin_application + rtcwin_client_api
rtcwin_ui                     -> rtcwin_application
rtc-talk                      -> rtcwin_ui + rtcwin_client_gateway
```

运行时只允许：

```text
rtc-talk.exe -> rtcwin_client_api -> rtcwin_engine.dll
```

禁止 UI 静态链接 WebRTC 或 Windows 采集实现。

## 13. 测试架构

### 13.1 测试替身

- `FakeSignalingTransport`
- `FakeClock`
- `FakeCameraService`
- `FakeAudioService`
- `FakeScreenService`
- `FakeMediaTransport`
- `FakeFrameSource`
- `FakeCredentialStore`

所有 Fake 位于 `tests/fixtures`，生产目标不得链接测试代码。

### 13.2 测试层级

```text
Unit
  domain reducers / protocol / state machines / layout engine

Component
  camera adapter / preview session / media coordinator with Fake transport

Integration
  engine + real signaling + fake media
  engine + real media + local loopback

End-to-end
  two real clients + real server + P2P/SFU

UI
  QtTest + screenshot + DPI matrix

Stability
  reconnect / device cycling / long run
```

### 13.3 必须有的反例测试

- 重复/乱序/迟到事件。
- 请求超时和取消。
- 设备权限拒绝。
- 摄像头被占用。
- 视频帧停止 5 秒。
- producer 在首帧前 paused。
- P2P 切 SFU 时视频中断。
- 关闭过程中收到回调。
- 日志目录不可写。
- 无效 UTF-8、超长消息、错误 JSON。

## 14. 构建和打包设计

### 14.1 配置

- Debug
- Release
- ReleaseWithSymbols

Release 必须生成 PDB，供现场符号化。

### 14.2 依赖锁定

- Qt 5.15.2 固定路径或 artifact。
- libwebrtc SDK 版本和 SHA256 记录在 `docs/integration-baseline.md`。
- vcpkg baseline 锁定。
- 所有 DLL 版本可查询。

### 14.3 安装包

包含：

- `rtc-talk.exe`
- `rtcwin_engine.dll`
- `log-runtime.dll`
- Qt Core/Gui/Widgets/Network
- Qt platforms 插件
- VC Runtime
- WebRTC 依赖
- 许可证和版本信息

不包含：

- demo 程序。
- UI 原型。
- 调试源码。
- 测试工具和联调脚本。

## 15. 为什么不在旧 demo 结构上继续改

以下旧结构不进入新工程：

| 旧结构 | 问题 | 新设计 |
|---|---|---|
| 巨型 `WebRtcMedia` | 采集、预览、发布、SDP、统计混在一起 | 分成 Camera/Audio/Screen/WebRTC/MediaCoordinator |
| 巨型 `CallSession` | 信令、房间、发言权、媒体命令集中 | 拆成 Room/Floor/Host/MediaMode 控制器 |
| `Media` 抽象包含所有能力 | 接口过宽，Fake 难实现 | 按业务能力拆成多个 port |
| UI 直接处理 JSON | 协议变化影响页面 | ClientGateway 转强类型事件 |
| 预览与发布共享一个 bool/flag | 生命周期互相干扰 | CaptureConsumer 引用计数与独立状态机 |
| 每帧直接更新 QWidget | UI 抖动、重绘过多 | FrameBridge + 33ms 合并 + Surface |
| demo 工具有效即视为生产完成 | 缺少边界和失败测试 | 每模块架构门禁与反例测试 |

## 16. 架构评审待确认项

编码前必须确认：

1. `rtc-talk-win` 是否为独立 Git 仓库，还是旧仓库的新分支加外部工作树。
2. 是否继续使用 Qt 5.15.2，还是直接采用 Qt 6。
3. 正式环境服务是否已经支持 WSS。
4. 是否允许引入 GoogleTest 作为测试依赖。
5. 是否要求记住 Token；如要求，明确使用 Credential Manager 还是 DPAPI。
6. 一期是否只支持主屏共享。
7. 诊断工具是否进入安装包，还是作为独立诊断包。
8. 是否允许使用现有 libwebrtc SDK 的 C++ API；UI 层仍强制走 C ABI。

## 17. 架构完成定义

详细架构评审通过后，实施计划必须以本文件为基线。若后续实现需要改变以下内容，
必须先新增 ADR：

- 进程/DLL 边界。
- 模块依赖方向。
- C ABI 形态。
- 线程模型。
- 领域状态所有权。
- 媒体管线拆分。
- 安全与日志边界。

架构未确认前，不创建业务代码，不提交实现，不进入任何开发阶段。
