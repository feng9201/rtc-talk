# rtc-talk-win 项目交接文档

> 快照日期：2026-10-30
> 仓库：`D:\google\webrtc-win-client\rtc-talk-win`
> 分支：`rtc-talk-win`
> 代码基线：`f2cc1a5 test: cover stopping camera during screen share`
>
> 当前状态：M1 视频场景、camera/screen 双路、共享主画面 + camera PIP 已完成并推送；Release/Debug 构建和 CTest 6/6 通过；SFU/P2P 双路及停止 camera/screen 均已截图验收。下一阶段为 M2 多路 Surface 与帧健康。
> 旧工程：`D:\google\webrtc-win-client\rtc-talk`，只读参考，禁止修改或提交。

本文档面向后续接入业务、继续媒体开发、做代码评审或发布验收的工程师。它描述当前真实代码状态，不把计划中的能力写成已完成能力。
## 0. 来源基线、参考分支和计划追溯

本节是交接的权威入口。后续实现、协议判断和验收必须以此处固定的仓库、分支、提交和文档为准。旧仓库工作树可能包含未提交变化，不能把“当前检出文件”当作权威版本。

### 0.1 仓库与固定参考基线

| 类别 | 仓库 / 分支 / 提交 | 本工程中的用途 | 权限 |
|---|---|---|---|
| 当前生产工程 | `D:\google\webrtc-win-client\rtc-talk-win` / `rtc-talk-win` / `f2cc1a5` | 真正交付工程、当前开发分支；文档更新以本文件所在提交为准 | 可开发、可提交到 `rtc-talk-win` |
| 服务端权威参考 | `D:\google\webrtc-win-client\rtc-talk` / `origin/main` / `8e6ff57` | 服务端行为、协议口径、S4 差异和联调依据 | 只读 |
| 旧 Windows 客户端功能参考 | `origin/feat/windows-client` / `df22708` | `client-win` demo、既有音视频功能和联调逻辑参考 | 只读 |
| 旧 Windows 生产实现参考 | `origin/codex/feat/windows-production-client` / `bbb3891` | P5 UI、生产模块、历史改造和验收记录参考 | 只读 |
| P5 UI 运行基线 | `D:\google\webrtc-win-client\rtc-talk\client-win\build\windows-x64\bin\Release\talk-desktop.exe` | 视觉和控件验收，不得自行改版 | 只读 |
| P5 UI 源码参考 | `D:\google\webrtc-win-client\rtc-talk\client-win\ui\desktop` | 控件结构、主题和房间页行为参考 | 只读 |
| muxPlayer 用户封装源码 | `rtc-talk-win/third_party/muxplayer` 及其依赖 | 产品内视频渲染基础设施 | 本仓库可维护 |
| libwebrtc SDK | `WEBRTC_SDK_DIR`，当前 M146 | 外部开发 SDK，不复制整库进仓库 | 外部依赖 |

强制规则：

1. 服务端协议以 `rtc-talk` 的 `origin/main@8e6ff57` 为准，不以本地 `main` 或旧仓库当前工作树为准。
2. 旧仓库当前工作树存在未提交变化，交接和排障必须注明固定远端提交，不得依赖其本地脏工作树。
3. 不修改、不提交 `main`、`feat/windows-client`、`codex/feat/windows-production-client` 或其他参考分支。
4. 本地 `client-win` demo 只能提供行为和实现参考，不能直接决定生产架构。
5. 所有旧客户端分支只作为只读对照；如发现逻辑问题，记录到本工程计划或问题单，不回写参考分支。

### 0.2 必须一起阅读的计划和协议文件

| 文件 | 权威内容 | 对本工程的作用 |
|---|---|---|
| `rtc-talk/plan/talk-protocol.md` | `session.hello/ping/refreshIce`、`room.join/leave`、`media.setMute`、`sfu.publish/offer/restartIce`、P2P、媒体模式切换、resume、PTT 和视频约束 | 信令与媒体协议的最高优先级参考 |
| `rtc-talk/plan/rtc-server-dev-plan.md` | 服务端房间、SFU consume/produce、P2P 分配、第三人切 SFU、失败回退和已知修复 | 判断服务端请求时序、错误码和联调行为 |
| `rtc-talk/plan/RK3576_客户端_开发计划_b524300f.md` | 跨平台客户端完整计划、P2P/SFU 踩坑、resume、事件时序和验收经验 | 客户端媒体状态机和回归风险参考 |
| `rtc-talk-win/plan/production-plan.md` | 当前 Windows 生产工程 S0-S8、P0-P5、门禁和维护规则 | 当前工程实施计划；文档内部基线可能早于当前提交，实施时以代码和本表状态为准 |
| `rtc-talk-win/plan/gap-driven-development-plan.md` | M1-M7 缺口驱动实施计划、阶段门禁和验收项 | 主计划的唯一执行分册，不是平行计划 |
| `rtc-talk-win/docs/requirements.md` | 产品功能和非功能要求 | 功能验收 |
| `rtc-talk-win/docs/architecture/*` | 当前生产工程架构设计 | 模块边界和后续重构 |
| `rtc-talk-win/docs/build-and-test.md` | 构建、依赖和测试说明 | 开发环境复现 |

协议实现发生分歧时，优先级为：

```text
服务端 origin/main 的协议实现与 talk-protocol.md
  > rtc-server-dev-plan.md
  > Windows 客户端参考分支
  > 当前客户端既有实现
```

如果当前实现与服务端协议冲突，必须记录冲突并修正客户端，不能为了兼容旧 demo 继续扩大偏差。

### 0.3 计划追溯矩阵

状态定义：

- **已完成并推送**：代码、构建和阶段验收已完成，提交在远端。
- **代码完成待验收**：实现已落盘并通过本地构建/短链路测试，但业务链路尚未验收。
- **部分完成**：核心能力已有，但计划要求尚未全部满足。
- **未开始**：没有可交付实现。

| 计划项 | 计划要求 | 当前状态 | 代码 / 测试 | 剩余验收 |
|---|---|---|---|---|
| S0 | 工程、依赖、分支和基线 | 已完成并推送 | `d49836f` | 无 |
| S1 | muxPlayer、本地摄像头、C ABI、P5 UI | 已完成并推送 | `de3a7f1`、摄像头预览、muxPlayer 截图 | 长稳定性测试按用户要求另行安排 |
| S2 | 信令、认证、房间生命周期、成员快照 | 已完成并推送 | `98f1ee8`、`rtcwin_signaling_tests`、双客户端联调 | 无 |
| S3.1 | libwebrtc 音频设备枚举与预入会选择 | 已完成并推送 | `53a8fdd`、`rtcwin_audio_devices_probe` | 无 |
| S3.2 | 音频 PeerConnection、P2P/SFU、静音、音量、收听、系统音 | 核心完成并推送，待完整人工验收 | `5ab73fa`、`MediaSession`、WASAPI loopback | 收听/音量听音、设备异常、离房重进和恢复 |
| S4.1 | 摄像头 SFU 发布、远端 I420、muxPlayer 房间渲染 | 代码完成，双端短链路通过 | `MediaSession`、MF H.264、`RemoteVideoSink`、`MuxVideoSurface` | 设备切换、离房重进、异常恢复 |
| S4.2 | 屏幕共享、P2P 视频、多路视频池和压力 | 传输和单路/双路呈现完成，性能与回收未完成 | 屏幕采集、SFU/P2P screen producer、camera/screen 双路截图 | 多路 session pool、9 路性能、5 秒无帧回收 |
| S5 | 成员、发言权、主持人、PTT/审核模式 | 部分完成 | 房间成员/策略/floor 快照已有；floor 请求仍为 501 | floor.request/release、revoked、keepalive、主持人操作 |
| S6 | 设备、统计、日志和诊断 | 部分完成 | 摄像头/音频设备枚举、`media.stats` 基础字段 | RTCStats、设备热插拔、设置弹窗、脱敏日志、诊断面板 |
| S7 | 重连、resume、ICE refresh、安全 | 未开始 | 当前关闭后重置媒体，无 resume | generation guard、WSS、resume、ICE restart、错误恢复 |
| S8 | 安装包、签名、升级和发布验收 | 未开始 | 无 | 安装包、依赖清单、签名、升级和发布验收 |
| P0 | 计划与依赖审计 | 已完成 | 本工程依赖边界和构建脚本 | 后续计划变更时维护 |
| P1 | muxPlayer 可构建 | 已完成并推送 | `de3a7f1` | 无 |
| P2 | muxPlayer 原始帧注入 | 已完成并推送 | `de3a7f1` | 远端帧接入在 S4 完成 |
| P3 | 本地摄像头采集与 C ABI | 已完成并推送 | `de3a7f1`、S4.1 增加远端帧 ABI v2 | 无 |
| P4 | UI 本地预览联调 | 已完成并推送 | P5 预入会页、真实摄像头截图 | 视觉改动必须继续对照 P5 |
| P5 | 第一里程碑稳定性与提交验收 | 已完成阶段验收 | Release/Debug 构建、既有联调 | 长稳定性测试按用户明确要求执行 |
| M1 | 视频场景、camera/screen 双路、共享主画面 + PIP | 已完成并推送 | `fff99ad`、`f2cc1a5`、`rtcwin_video_scene_tests`、SFU/P2P 截图 | 仅待用户按阶段流程确认，无技术缺口 |

### 0.4 当前计划偏差

必须明确记录以下偏差：

1. `plan/production-plan.md` 是长期 S0-S8 计划；当前执行基线以本文
   `f2cc1a5` 和 `plan/gap-driven-development-plan.md` 的 M1-M7 为准。
2. `talk-protocol.md` 定义了 resume、ICE restart、P2P/SFU 先建后断、视频 m-line 等完整约束；当前 SFU 摄像头/屏幕共享和 P2P 音视频三 m-line 已接入，resume/ICE restart 仍未实现。
3. 第三人加入“先建 SFU、确认可用、再关 P2P”已在短链路验证，后续仍需覆盖网络异常和重复切换。
4. 服务端计划包含 `p2p.state`、P2P 失败回退、10 秒超时和 candidate 缓存等约束，当前客户端只完成基础转发，没有完整恢复策略。
5. 计划中的 S5-S8、S4.2 多路 session pool/压力、设备热插拔、WSS、resume 和安装包均不能在交接中写成已完成。

---


---

## 1. 先看这里：当前结论

### 1.1 当前已经具备

- Windows x64 Qt Widgets 桌面客户端工程。
- P5 预入会页、房间页和底部圆形控制栏基线。
- Windows Media Foundation 摄像头枚举、采集、NV12 -> I420。
- muxPlayer 完整源码和用户封装依赖已迁入本仓库。
- 本地摄像头经 muxPlayer 软件 OpenGL 路径出画。
- WebSocket 信令、开发 Token、`session.hello`、`room.join/leave`、心跳。
- 房间成员、角色、策略、发言权、媒体模式等快照与增量通知。
- libwebrtc M146 的 Windows Core Audio 设备枚举。
- 预入会页真实录音/播放设备列表。
- 统一 `MediaSession`：
  - Windows Core Audio ADM。
  - Opus 编解码工厂。
  - APM/3A。
  - 本地音频轨。
  - SFU publish offer/answer。
  - SFU subscribe offer/answer。
  - P2P offer/answer/ICE candidate。
  - 静音、收听静音、音量、SFU 暂停。
  - P2P 连通后暂停 SFU 音频，避免双路重复播放。
  - Media Foundation H.264 编解码工厂。
  - 同一 SFU PeerConnection 内的摄像头视频轨。
  - 远端视频 I420 拆包和 frame callback。
- Engine 内已接入：
  - `sfu.publish`
  - 服务端 `sfu.offer`
  - 服务端 `room.mediaMode`
  - `p2p.offer/answer/candidate/close`
  - `media.setMute`
  - `media.setVolume`
  - `media.setDeafened`
  - `media.stats`
- UI Gateway 已能消费：
  - `media_enabled`
  - `media.ready`
  - `media.published`
  - `media.muted`
  - `media.deafened`
  - `media.error`
- S4.1 摄像头视频链路：
  - `MediaSession` 在同一 PeerConnection 内发布音频和摄像头 H.264。
  - Media Foundation H.264 编码/解码工厂。
  - `AdaptedVideoTrackSource` 接收 Engine 摄像头 I420。
  - `sfu.offer.consumers[]` 的 `mid/uid/kind/source` 映射。
  - 远端 I420 经 C ABI v2 返回 UI。
  - 房间页 `VideoGrid`/`VideoTile` 通过 `MuxVideoSurface` 显示本地和远端视频。
- 服务端 S4.1 双端短链路已验证：
  - 发送端 `camera_frames=31 -> 179`。
  - 发送端 `video_packets_sent=14 -> 293`。
  - 接收端 `video_packets_received=53 -> 325`。
- 诊断日志依赖：
  - `log-cpp[shared]@1.0.0#4` 由私有 vcpkg registry 引入。
  - Engine 通过 `rtcwin_log_adapter.dll` C ABI 调用 log-cpp。
  - `/MT` Engine 与 `/MD` log-cpp 之间不传递 STL 类型。

### 1.2 当前阶段确认与剩余验收

M1 已通过技术门禁：

- camera/screen 使用 `uid + source` 独立 Tile。
- SFU、P2P 双端 camera/screen 已截图。
- 共享主画面 + camera PIP 已完成。
- 停止 screen 后回退到 camera Grid。
- 停止 camera 后 screen 保留、PIP 消失。
- Release/Debug 构建通过，CTest 6/6 通过。

M1 目前只等待用户按阶段流程确认。仍未验收的生产能力：

- 多路视频 Surface 池、9 路性能、5 秒无帧回收。
- Floor/PTT、主持人授权、全员静音、踢人和策略更新。
- 会议中设备切换、热插拔、多屏/窗口共享和系统音端点选择。
- resume、ICE refresh/restart 和 P2P 失败回退。
- 完整 RTCStats、状态面板、诊断包导出。
- WSS/TLS、Token 安全持久化和正式安装发布。

### 1.3 当前工作树状态

当前分支 `rtc-talk-win` 与远端同步，工作树干净。

- M1 代码提交：`f2cc1a5`。
- M1 场景模型提交：`fff99ad`。
- 当前 CTest 集：6 个测试，Release/Debug 均 6/6。
- 最终便携包：`dist/rtc-talk-win-20261030-132934.zip`。
- M1 证据和包 SHA256：见 §18.12。

### 1.4 计划入口与当前下一步

项目只保留一套计划体系，不在 `handover.md` 重复定义另一套排期：

- `plan/production-plan.md`：长期主计划，保留原始 S0-S8、P0-P5 范围。
- `plan/gap-driven-development-plan.md`：当前唯一执行分册，定义 M1-M7
  顺序、接口、验收和估算。
- `handover.md`：只记录当前状态、证据、风险、提交和下一步，不复制计划正文。

| 执行阶段 | 主计划映射 | 状态 |
|---|---|---|
| M1 视频场景和双路呈现 | S4.2 UI/双流呈现 | 已完成，待用户确认 |
| M2 多路 Surface 和帧健康 | S4.2 剩余部分 | 下一阶段 |
| M3 Floor、PTT 和主持人 | S5 | 未开始 |
| M4 房间设备与屏幕源 | S6 | 未开始 |
| M5 重连、resume 和 ICE 恢复 | S7 | 未开始 |
| M6 诊断和可观测性 | S6/S7 | 未开始 |
| M7 安全和发布 | S7/S8 | 未开始 |

当前下一步只有一项：用户确认 M1 后，按执行分册开始 M2。M2-M7 的详细范围、
接口、验收和估算不得在交接文档中另写一套。

---

## 2. 项目宗旨和不可破坏的约束

### 2.1 项目宗旨

`rtc-talk-win` 是正式生产工程，不是 `client-win` demo 的换皮版本。

它的目标是：

```text
Qt Widgets UI
  -> 应用控制器
  -> 版本化 C ABI
  -> 媒体引擎 DLL
  -> libwebrtc / 摄像头 / 音频设备
  -> muxPlayer 视频渲染
```

生产原则：

- UI 不在 UI 线程直接操作 libwebrtc。
- UI 不直接链接 libwebrtc。
- libwebrtc 只在 Engine 内部使用。
- 用户自有封装库必须完整存入本仓库。
- Qt、Visual Studio、CMake、vcpkg 属于开发环境，可通过环境变量引用。
- 产品运行时不得依赖其他工程的绝对路径。
- 旧 `rtc-talk` 只读。
- 不改 `main` 或其他分支。
- 未经明确要求，不运行长时间稳定性测试。
- P5 是 UI 基线，不允许重新“创意设计”。

### 2.2 UI 基线

参考程序和源码：

```text
D:\google\webrtc-win-client\rtc-talk\client-win\build\windows-x64\bin\Release\talk-desktop.exe
D:\google\webrtc-win-client\rtc-talk\client-win\ui\desktop
```

已有截图：

```text
D:\google\webrtc-win-client\rtc-talk-win\build\screenshots\s2-room-controls-p5.png
D:\google\webrtc-win-client\rtc-talk-win\build\screenshots\s3-audio-devices.png
```

房间控制栏基线：

- 左侧：闭麦、摄像头、共享屏幕。
- 右侧：收听、设置、离开。
- 使用圆形 `IconButton`，不要恢复成错误的大圆角矩形按钮。
- 顶部人数和 `3/9` 等文案必须完整显示。

---

## 3. 当前系统架构

### 3.1 进程与模块拓扑

```mermaid
flowchart LR
  subgraph UI["rtc-talk.exe (/MD)"]
    Qt[Qt Widgets P5 UI]
    App[AppController]
    Gateway[CAbiEngineClient]
    Mux[muxPlayer Adapter]
    Surface[Mux GL Surface]
    Qt --> App --> Gateway
    App --> Mux --> Surface
  end

  subgraph ABI["C ABI"]
    API[rtcwin_client_api.dll boundary]
  end

  subgraph Engine["rtcwin_engine.dll (/MT)"]
    Core[EngineCore]
    Worker[Engine command worker]
    SigWorker[Signaling io_context thread]
    Audio[MediaSession]
    Camera[CameraCaptureSession]
    Core --> Worker
    Core --> SigWorker
    Worker --> Audio
    Worker --> Camera
  end

  Gateway --> API --> Core
  Audio --> RTC[libwebrtc M146]
  SigWorker --> WS[WebSocket / talk.v1.json]
```

### 3.2 分层

| 层 | 目录 | 职责 |
|---|---|---|
| Domain | `libs/domain` | 纯领域类型、快照、错误、恢复动作 |
| Application | `libs/application` | Qt `QObject` 控制器、设置持久化、UI 与引擎边界 |
| Client API | `libs/client_api` | 稳定 C ABI |
| Engine | `libs/engine` | 命令队列、信令编排、房间/媒体状态、C ABI 实现 |
| Client Gateway | `libs/client_gateway` | `QNetworkAccessManager` 开发 Token、C ABI -> Qt signal |
| Log Adapter | `libs/log_adapter` | `/MD` 日志 DLL，隔离 vcpkg log-cpp 与 `/MT` Engine |
| UI | `libs/ui` | P5 Qt Widgets 页面、主题、控件、muxPlayer surface |
| Camera | `libs/camera_win` | Media Foundation 枚举和采集 |
| Signaling | `libs/signaling` | WebSocket、信封校验、请求应答、心跳基础 |
| WebRTC Media | `libs/webrtc_media` | ADM、音视频会话、H.264、SFU/P2P 媒体 |
| Mux Adapter | `libs/muxplayer_adapter` | I420 到 muxPlayer raw frame 注入 |

### 3.3 线程模型

当前实际线程：

| 线程 | 所有者 | 作用 |
|---|---|---|
| Qt GUI 线程 | `rtc-talk.exe` | QWidget、信号槽、画面提交 |
| Engine command worker | `EngineCore::worker_` | C ABI 命令、媒体对象生命周期、设备操作 |
| Signaling io_context | `EngineCore::signaling_thread_` | WebSocket、请求应答、心跳 timer |
| libwebrtc network thread | `MediaSession` | ICE/DTLS/RTP 网络 |
| libwebrtc worker thread | `MediaSession` | ADM、编解码、PeerConnection 工作 |
| libwebrtc signaling thread | `MediaSession` | PeerConnection/SDP 操作 |
| Audio capture/render | Windows/libwebrtc | Core Audio 实时回调 |

关键线程规则：

1. `MediaSession` 的公开控制方法必须由 Engine worker 串行调用。
2. `SignalingClient` 只在 `signaling_io_` 线程访问。
3. 信令回调不能在 signaling 线程直接操作媒体，必须 `PostToWorker(...)`。
4. 媒体事件回调不能在 libwebrtc 线程直接操作 Engine，必须 `PostToWorker(...)`。
5. C ABI 回调可以跨线程，Gateway 使用 Qt queued invocation 回到 GUI 线程。
6. 析构顺序：先停止信令，再停止 worker，最后销毁媒体。
7. ADM 和 `AudioDeviceBuffer` 有线程亲和性，最后一个 ADM 引用必须在创建它的 worker 线程释放。

### 3.4 ABI/运行时约束

- Engine：`/MT` 或 `/MTd`。
- UI：`/MD` 或 `/MDd`，不得直接链接 WebRTC。
- `libs/webrtc_media`：C++20、`/MT`、`/EHs-c-`、RTTI 开启。
- `libs/log_adapter`：`/MD` 或 `/MDd`，只暴露 C ABI。
- Engine 只向日志适配层传 `const char*`，不得跨 DLL 传 `std::string`、异常或 STL 容器。
- Debug 必须定义 `_HAS_ITERATOR_DEBUGGING=0`。
- WebRTC SDK 当前版本：M146。
- SDK 路径通过 `WEBRTC_SDK_DIR` 注入。
- 如果修改 WebRTC ABI 宏、异常模型、运行库或 iterator debugging，常见结果是 `LNK2038`。

当前存在的预期编译告警：

- `D9025`: CMake 默认 `/EHsc` 被 WebRTC ABI 要求的 `/EHs-c-` 覆盖。不要为了消警告把媒体模块改回 `/EHsc`。

---

## 4. 模块和关键文件

### 4.1 Domain / Application / UI

| 文件 | 作用 |
|---|---|
| `libs/domain/include/rtcwin/domain/types.h` | 会话快照、Peer、设备、策略、媒体模式、UI 状态 |
| `libs/application/include/rtcwin/application/app_controller.h` | UI 控制器接口 |
| `libs/application/src/app_controller.cpp` | 入房、离房、麦克风、摄像头、共享、收听等 UI 命令编排 |
| `libs/application/include/rtcwin/application/engine_client.h` | Application -> Engine 抽象 |
| `libs/client_gateway/src/c_abi_engine_client.cpp` | 开发 Token、C ABI 调用、事件解析、快照更新 |
| `libs/ui/src/pages/prejoin_page.cpp` | P5 预入会页和设备选择 |
| `libs/ui/src/pages/room_page.cpp` | P5 房间页和控制栏 |
| `libs/ui/src/video/*` | 本地/远端视频表面和 muxPlayer 适配 |
| `libs/ui/src/main_window.cpp` | 页面导航和信号连接 |

### 4.2 Engine

| 文件 | 作用 |
|---|---|
| `libs/engine/src/engine_core.cpp` | 线程、命令队列、模块装配和 C ABI 内核入口 |
| `libs/engine/src/session_controller.cpp` | 连接、hello、入房、离房、心跳和信令回调 |
| `libs/engine/src/media_orchestrator.cpp` | 摄像头、媒体命令、SFU/P2P 协商和媒体事件 |
| `libs/engine/src/session_state_machine.cpp` | 会话 phase/media mode/generation 状态机 |
| `libs/engine/src/client_api.cpp` | C ABI 实例化和 Submit |

Engine 当前关键职责：

- `EngineCore`：线程、命令队列、模块装配、生命周期
- `SessionController`：连接、入房、离房、心跳、信令回调
- `MediaOrchestrator`：摄像头、SFU/P2P、媒体命令和统计
- `SessionStateMachine`：会话状态和 generation 隔离
- `ApplyP2pAnswer`
- `ApplyP2pCandidate`
- `HandleMediaModeRequest`
- `HandleSfuOfferRequest`
- `HandleMediaEvent`
- `HandleMediaSetMute`
- `HandleMediaSetVolume`
- `HandleMediaSetDeafened`
- `HandleMediaStats`

### 4.3 Signaling

| 文件 | 作用 |
|---|---|
| `libs/signaling/include/rtcwin/signaling/protocol.h` | `talk.v1.json` 信封、request/response/notification 构造 |
| `libs/signaling/include/rtcwin/signaling/signaling_client.h` | WebSocket 请求、超时、关闭原因 |
| `libs/signaling/src/signaling_client.cpp` | websocketpp + asio 实现 |

当前限制：

- 只支持 `ws://`，Engine 入房校验明确拒绝 `wss://`。
- 公开头文件仍暴露 asio/websocketpp 类型，尚未 PImpl。
- 尚未实现完整重连、resume、generation guard。
- 请求超时 10 秒，SFU 方法 15 秒。

### 4.4 音视频媒体

核心文件：

```text
libs/webrtc_media/include/rtcwin/webrtc_media/media_session.h
libs/webrtc_media/src/media_session.cpp
libs/webrtc_media/include/rtcwin/webrtc_media/audio_device_service.h
libs/webrtc_media/src/audio_device_service.cpp
libs/webrtc_media/src/video_track_source.cpp
libs/webrtc_media/src/remote_video_sink.cpp
libs/webrtc_media/src/video/webrtc_h264_factory.cpp
```

`MediaSession` 是音视频统一、PImpl、无 libwebrtc 公共类型泄漏的媒体运行时。

内部组成：

- libwebrtc network/worker/signaling 线程。
- Windows Core Audio ADM 或 Dummy ADM。
- Opus encoder/decoder factory。
- APM：AEC、NS、AGC2、高通。
- `CaptureGate`：编码前静音和采集帧计数。
- 本地 `AudioTrackInterface`。
- 本地 `CameraVideoTrackSource` 和 `VideoTrackInterface`。
- Media Foundation H.264 encoder/decoder factory。
- `sfu-send` PeerConnection。
- `sfu-recv` PeerConnection。
- `p2p` PeerConnection。
- 远端音频轨列表、音量和收听静音。
- 远端视频轨、consumer metadata 和 `RemoteVideoSink`。
- 5 秒 SDP 超时保护。

重要实现细节：

- 录音设备按 GUID/名称匹配；未匹配时回退第一个设备。
- 播放设备按 GUID/名称匹配；失败时回退默认通信设备。
- 真实设备路径 `InitPlayout` + `StartPlayout`。
- Debug 下 ADM 必须在 worker 线程释放，否则 `AudioDeviceBuffer` 会触发 thread checker fatal。
- `audio_source` 支持 `mic` 和 `system`。系统音通过默认播放设备的 WASAPI
  shared-loopback 采集，统一转换为 48 kHz 单声道 PCM 后进入 WebRTC。
- `SetMuted` 同时控制 CaptureGate、AudioTrack enabled 和 RTP sender active。
- 服务端拒绝开麦时会保持静音，避免误开麦。
- `SetSfuPaused(true)` 只暂停 SFU 收发，不影响已连通的 P2P 音频。
- `SetDeafened(true)` 只影响远端音频轨，不停止本地麦克风发送。
- 摄像头 sender、音频 sender 共用 `sfu-send` PeerConnection；不能为视频新建独立
  PC，否则 mediasoup transport 的 DTLS 绑定不一致。
- 摄像头帧时间戳在媒体边界统一改成本地单调时钟，不能直接使用可能回退的 MF
  device timestamp。

### 4.5 摄像头与 muxPlayer

摄像头：

- `libs/camera_win` 使用 Media Foundation。
- 输出 I420。
- Engine 通过 `rtcwin_options.on_local_frame` 交给 UI。
- 房间内开启摄像头后，同一 I420 帧同时进入 `MediaSession` 编码和本地预览。
- 远端 I420 通过 `rtcwin_options.on_remote_frame` 回到 Gateway。

muxPlayer：

```text
third_party/muxplayer
third_party/mtAudio
third_party/mtAudioPlayer
third_party/tmcore
third_party/flvparser
libs/muxplayer_adapter
```

当前渲染路径：

```text
CameraCaptureSession
  -> I420 callback
  -> CAbiEngineClient latest-frame bridge
  -> MuxPlayer Adapter
  -> muxPlayer raw I420 live render
  -> GL surface

sfu.offer.consumers[]
  -> mid/uid/source registry
  -> MediaSession RemoteVideoSink
  -> C ABI v2 remote I420 callback
  -> CAbiEngineClient latest-frame-per-stream bridge
  -> MuxVideoSurface
```

原则：

- 不复用 muxPlayer 的 H.264/H.265 输入接口做“WebRTC 解码后再编码再解码”。
- WebRTC 已解码的 I420 应直接注入 muxPlayer。
- 摄像头 SFU 发布和远端 muxPlayer 渲染已完成短链路验证。
- 屏幕共享和 P2P 音视频三 m-line 已接入；多路 session pool 仍待完成。

---

## 5. 依赖策略

### 5.1 仓库内依赖

必须保留源码：

- `third_party/muxplayer`
- `third_party/mtAudio`
- `third_party/mtAudioPlayer`
- `third_party/tmcore`
- `third_party/flvparser`
- `third_party/websocketpp`

这些是用户封装或需要随业务定制的依赖。

### 5.2 外部开发依赖

| 依赖 | 来源 | 原因 |
|---|---|---|
| Qt 5.15 | `QTDIR` | 开发环境，不复制进仓库 |
| Visual Studio 2022 | 本机 | 编译环境 |
| CMake | 本机 | 构建系统 |
| libwebrtc M146 | `WEBRTC_SDK_DIR` | 开源外部 SDK，约 2.3 GiB；不复制源码/二进制到仓库 |
| FFmpeg | 私有 vcpkg registry | 通过 `vcpkg.json` 固定版本 |
| log-cpp | 私有 vcpkg registry | `log-cpp[shared]@1.0.0#4`，与 demo 一致 |
| asio | vcpkg | signaling |
| nlohmann-json | vcpkg | 协议和 JSON |

当前机器环境示例：

```powershell
$env:VCPKG_ROOT = 'D:\vcpkg'
$env:QTDIR = 'C:\Qt\5.15.2\msvc2019_64'
$env:WEBRTC_SDK_DIR = 'D:\google\webrtc_sdk_hevc'
```

不得把上述绝对路径硬编码到产品代码或安装产物中。

---

## 6. 构建、运行和测试

### 6.1 当前构建目录

实际使用：

```text
build/windows-x64
```

Release 产物：

```text
build/windows-x64/bin/Release/rtc-talk.exe
build/windows-x64/bin/Release/rtcwin_engine.dll
build/windows-x64/bin/Release/rtcwin_log_adapter.dll
build/windows-x64/bin/Release/log-cpp.dll
```

Debug 产物：

```text
build/windows-x64/bin/Debug/rtc-talk.exe
build/windows-x64/bin/Debug/rtcwin_engine.dll
build/windows-x64/bin/Debug/rtcwin_log_adapter.dll
build/windows-x64/bin/Debug/log-cpp.dll
```

### 6.2 配置

```powershell
$env:VCPKG_ROOT = 'D:\vcpkg'
$env:QTDIR = 'C:\Qt\5.15.2\msvc2019_64'
$env:WEBRTC_SDK_DIR = 'D:\google\webrtc_sdk_hevc'

cmake --preset windows-x64 -B build\vcpkg-windows-x64
```

### 6.3 Release 构建和测试

```powershell
cmake --build build\vcpkg-windows-x64 --config Release --parallel
ctest --test-dir build\vcpkg-windows-x64 -C Release --output-on-failure
```

### 6.4 Debug 构建和测试

```powershell
cmake --build build\vcpkg-windows-x64 --config Debug --parallel
ctest --test-dir build\vcpkg-windows-x64 -C Debug --output-on-failure
```

### 6.5 运行 UI

```powershell
$env:PATH = "$env:QTDIR\bin;" + $env:PATH
.\build\vcpkg-windows-x64\bin\Release\rtc-talk.exe
```

需要 `rtcwin_engine.dll`、Qt DLL、FFmpeg DLL 在运行路径或 `PATH` 中。

### 6.6 音频短链路探针

默认 fake ADM，不依赖真实设备：

```powershell
.\build\vcpkg-windows-x64\bin\Release\rtcwin_media_session_smoke.exe
```

可选真实设备：

```powershell
$env:RTCWIN_AUDIO_BACKEND = 'wasapi'
.\build\vcpkg-windows-x64\bin\Release\rtcwin_media_session_smoke.exe
Remove-Item Env:RTCWIN_AUDIO_BACKEND
```

该探针会：

- 初始化 MediaSession。
- 生成 SFU publish offer。
- 用第二个 fake 会话生成 SFU subscribe answer。
- 应用 answer。
- 生成 P2P offer/answer。
- 测试静音、收听、SFU 暂停、音量。
- 立即关闭，不做长时间稳定性测试。

### 6.7 音频设备枚举

```powershell
.\build\vcpkg-windows-x64\bin\Release\rtcwin_audio_devices_probe.exe
```

### 6.8 信令冒烟

```powershell
$env:RTCWIN_SIGNALING_TOKEN = '<token>'
.\build\vcpkg-windows-x64\bin\Release\rtcwin_signaling_smoke.exe `
  --url ws://47.117.159.193:8080/talk/v1/ws `
  --uid rtcwin-smoke `
  --room rtcwin-smoke
```

开发 Token 获取：

```text
GET http://47.117.159.193:8080/dev/token?uid=<uid>&rooms=<room>
```

不要在源码、提交记录或文档中保存真实 Token。

---

## 7. 信令与入房流程

### 7.1 入房总流程

```mermaid
sequenceDiagram
  participant UI as Qt UI
  participant GW as CAbiEngineClient
  participant E as EngineCore
  participant S as Signaling
  participant M as MediaSession

  UI->>GW: ConnectAndJoin(options)
  GW->>GW: /dev/token（可选）
  GW->>E: session.connectAndJoin
  E->>S: WebSocket Connect
  S->>E: on_open
  E->>S: session.hello
  S-->>E: session_id / ice_servers / heartbeat
  E->>S: room.join
  S-->>E: room snapshot + media_mode
  E->>M: Initialize(audio config + ICE)
  M-->>E: ready
  E-->>GW: response(media_enabled)
  E-->>UI: session.ready
  alt media_mode == p2p
    E->>M: CreateP2pOffer/等待远端 offer
    E->>S: p2p.offer
  else SFU
    E->>M: CreateSfuPublishOffer
    E->>S: sfu.publish
    S-->>E: answer
    E->>M: ApplySfuPublishAnswer
  end
```

当前 `session.hello` 能力：

```json
{
  "client": {
    "platform": "windows-x64",
    "version": "0.1.0",
    "sdk": "rtcwin-s3"
  },
  "caps": {
    "audio": {"codecs": ["opus"], "aec": "webrtc"},
    "p2p": true,
    "p2p_video": false,
    "sfu_sdp": "unified-plan"
  }
}
```

当前 `room.join` 关键字段：

```json
{
  "room_id": "<room>",
  "display": "<display>",
  "media": {
    "audio": {"send": true, "muted": true},
    "video": {"send": false}
  },
  "policy": {
    "talk_mode": "free|ptt|moderated",
    "media_mode": "auto|sfu|p2p"
  }
}
```

注意：

- 入房初始麦克风是静音状态。
- 媒体初始化完成后，`session.connectAndJoin` response 会附加 `media_enabled`。
- 如果音频初始化失败，当前实现仍允许房间入房成功，但 `media_enabled=false` 并发送 `media.error`。后续是否改为入房失败需要产品确认。
- `p2p_video=false`，因为当前只接入了 P2P 音频；视频只走 SFU。

### 7.2 服务端请求

当前处理：

| method | 处理 |
|---|---|
| `room.mediaMode` | 切换到 `p2p` 或 `sfu` |
| `sfu.offer` | 创建 SFU subscribe answer 并返回 `{"sdp": ...}` |
| 其他 | `501` |

风险：

- 当前 `room.mediaMode=sfu` 在本地 offer 创建后立即返回成功，不等待订阅链路真正完成。旧 demo 的实现更严格，后续应改成等待 SFU subscribe 成功或超时再响应。
- `sfu.offer` 若在 `MediaSession` 初始化前到达，会返回 409。需要确认服务端是否重试。

### 7.3 P2P 通知

| method | 方向 | 当前处理 |
|---|---|---|
| `p2p.offer` | server -> client | answerer 创建 answer，回 `p2p.answer` |
| `p2p.answer` | server -> client | offerer 应用 answer |
| `p2p.candidate` | server -> client | 转交 MediaSession |
| `p2p.state` | client -> server | 上报连接状态 |
| `p2p.close` | server -> client | 只关闭 P2P PC，不销毁 SFU/音频设备 |

P2P 角色来自 `room.mediaMode` 的 `p2p` 分配：

```json
{
  "session": "<session>",
  "peer": "<uid>",
  "role": "offerer|answerer"
}
```

如果缺少分配：

- 当前会发 `p2p.state failed`。
- 回退 SFU publish。

### 7.4 模式切换

```mermaid
stateDiagram-v2
  [*] --> SfuPublishing
  SfuPublishing --> P2pConnecting: room.mediaMode=p2p
  P2pConnecting --> P2pConnected: ICE connected
  P2pConnected --> SfuPaused: 暂停 SFU 音频
  SfuPaused --> SfuPublishing: room.mediaMode=sfu
  SfuPublishing --> P2pConnecting: 新 P2P 分配
```

当前实现：

- P2P connected 后设置 `sfu_paused=true`。
- SFU sender 和 SFU 远端track暂停。
- P2P 不通时恢复 SFU。
- 切回 SFU 时关闭 P2P，恢复 SFU 并重新 publish。

尚未验证：

- 真实 1v1 双向音频。
- 第三人加入触发 SFU 后无重复音、无断音。
- 再次切回 P2P 的 DTLS/SDP 重协商。

---

## 8. 当前 C ABI 命令和事件

### 8.1 命令

当前 Engine 支持：

| method | payload 关键字段 |
|---|---|
| `engine.ping` | 无 |
| `camera.devices` | 无 |
| `camera.preview.start` | `device_index` |
| `camera.preview.stop` | 无 |
| `audio.devices` | 无 |
| `session.connectAndJoin` | URL、用户、房间、Token、设备、模式、音量 |
| `media.setMute` | `{"muted": bool}` |
| `media.setVolume` | `{"gain": 0..1}` 或 `{"volume": 0..100}` |
| `media.setDeafened` | `{"deafened": bool}` |
| `media.stats` | 无 |
| `room.leave` | 无 |
| `session.disconnect` | 无 |

目前 CAbi 还会发送但 Engine 尚未实现：

| method | 当前结果 |
|---|---|
| `media.toggleCamera` | 501 |
| `media.toggleScreenShare` | 501 |
| `floor.request` | 501 |
| `floor.release` | 501 |

### 8.2 `session.connectAndJoin` 当前 payload

```json
{
  "url": "ws://...",
  "user_id": "...",
  "room_id": "...",
  "token": "...",
  "fetch_dev_token": true,
  "display": "...",
  "volume": 60,
  "recording_device": "<adm guid/name>",
  "playout_device": "<adm guid/name>",
  "audio_backend": "wasapi|fake",
  "audio_source": "mic|system",
  "media_mode": "auto|sfu|p2p",
  "talk_mode": "free|ptt|moderated"
}
```

当前 `audio_source=system` 使用 `SwitchableAudioDevice + LoopbackAudioCapture`。
底层 Core Audio ADM 继续负责播放和对端声音，录音路径替换为默认播放设备 loopback。

### 8.3 媒体通知

| method | data |
|---|---|
| `media.ready` | 录音/播放设备名 |
| `media.publishing` | mode、SDP 大小 |
| `media.published` | `ok=true`、mode |
| `media.error` | code、message、detail |
| `media.muted` | muted、forced |
| `media.deafened` | deafened |
| `media.state` | libwebrtc PC 连接状态 |
| `media.track` | 远端音频轨事件 |
| `p2p.started` | session、peer、role |
| `p2p.state` | connecting/connected/failed/closed |
| `p2p.candidate` | candidate、sdp_mid、index、session、to |

### 8.4 C ABI 信封

Response：

```json
{
  "v": 1,
  "type": "res",
  "id": 123,
  "method": "media.setMute",
  "ok": true,
  "data": {}
}
```

Notification：

```json
{
  "v": 1,
  "type": "ntf",
  "method": "media.muted",
  "data": {"muted": true}
}
```

请求：

```json
{
  "v": 1,
  "type": "req",
  "id": "server-request-id",
  "method": "sfu.offer",
  "data": {"sdp": "..."}
}
```

失败响应必须包含：

```json
{"ok": false, "error": {"code": 500, "msg": "..."}}
```

---

## 9. MediaSession 使用规则

### 9.1 初始化

```cpp
MediaConfig config;
config.audio_backend = "wasapi";
config.audio_source = "mic";
config.recording_device = "<guid-or-name>";
config.playout_device = "<guid-or-name>";
config.volume = 60;
config.muted = true;
config.ice_servers = ...;

MediaSession session;
MediaError error;
session.Initialize(config, event_callback, remote_video_callback, &error);
```

初始化会：

- 创建 WebRTC 线程。
- 创建并初始化 ADM。
- 选择录音和播放设备。
- 初始化并启动 playout。
- 创建 APM。
- 创建 PeerConnectionFactory。
- 创建本地 AudioTrack。
- 保存 ICE server 配置。

### 9.2 生命周期

- 一个房间会话创建一个 `MediaSession`。
- `Shutdown()` 可重复调用。
- `Shutdown()` 必须从 Engine worker 调用，不要从 WebRTC 回调调用。
- `CloseP2p()` 只关闭 P2P，不影响 SFU 和设备。
- `ResetMedia()` 关闭全部媒体并清空状态。

### 9.3 API 语义

| API | 语义 |
|---|---|
| `CreateSfuPublishOffer` | 创建/复用 sfu-send PC，添加音频和摄像头轨，生成本地 offer |
| `ApplySfuPublishAnswer` | 应用 SFU answer，恢复 sender 状态 |
| `CreateSfuSubscribeAnswer` | 创建/复用 sfu-recv PC，写入 consumers mid/uid/source 映射，应用远端 offer，生成 answer |
| `CreateP2pOffer` | 新 PC、sendrecv 音频轨、生成本地 offer |
| `CreateP2pAnswer` | 新 PC、先应用远端 offer、AddTrack、生成 answer |
| `ApplyP2pAnswer` | offerer 应用 answer |
| `AddP2pCandidate` | 添加远端 ICE 候选 |
| `SetMuted` | 控制上行 |
| `SetDeafened` | 控制全部远端音频轨 |
| `SetSfuPaused` | 控制 SFU 上行和 SFU 远端轨，不影响 P2P |
| `SetVolume` | 0.0 - 1.0 |
| `SetCameraEnabled` | 控制摄像头 RTP sender 和本地编码开关 |
| `PushCameraFrame` | 将 Engine 的 I420 摄像头帧送入视频源 |

### 9.4 当前限制

- 系统音只采集 Windows 默认播放端点；当前不支持在预入会页选择任意播放端点。
- 没有设备热插拔监听。
- 没有 RTCStats RTT/码率/丢包采集；目前只有连接态、采集帧、远端轨数量和设备名。
- 没有音频电平事件。
- 没有显式 audio jitter buffer 调优。
- 没有服务端 ICE restart/DTLS 重建策略。
- P2P 和 SFU 的 candidate 事件只转发了必要的 P2P 字段。
- 摄像头、屏幕共享和 P2P 视频均已接入；当前验收覆盖双人/F 双端短链路。
- 多路视频仍是一路一个 muxPlayer surface，尚未做 session pool 和 9 路压力验收。

---

## 10. 已验证结果

### 10.1 构建

当前工作树已执行：

- Release 全量构建：通过。
- Debug 全量构建：通过。
- `rtcwin_media_session_smoke` Release：通过。
- `rtcwin_media_session_smoke` Debug：通过。
- `rtcwin_log_adapter_smoke` Release：通过。
- `rtcwin_log_adapter_smoke` Debug：通过。
- `ctest` Release：4/4 通过。
- `ctest` Debug：4/4 通过。
- `git diff --check`：通过。

### 10.2 fake ADM 短链路

测试输出：

```text
audio media smoke ok sfu_sdp=2658 p2p_sdp=1024 recording=fake-microphone playout=fake-speaker
```

含义：

- 初始化成功。
- SFU publish offer 生成成功，包含音频和 H.264 视频 m-line。
- 第二个会话生成 SFU subscribe answer 成功。
- 第一个会话应用 SFU answer 成功。
- P2P offer/answer 成功。
- 静音、收听、SFU pause、音量 API 成功。
- 摄像头源接收合成 I420 帧成功。
- 未验证真实 ICE 连通。

### 10.3 真实 Windows Core Audio 短链路

当前机器使用：

```powershell
$env:RTCWIN_AUDIO_BACKEND = 'wasapi'
.\build\vcpkg-windows-x64\bin\Release\rtcwin_media_session_smoke.exe
```

结果：

```text
real audio device smoke ok sfu_sdp=1012 recording=麦克风 (5- Logitech Webcam C925e) playout=Headphones (High Definition Audio Device)
```

说明：

- 真实 ADM 初始化通过。
- 真实录音设备选择通过。
- 真实播放设备选择通过。
- 真实设备路径可生成 SDP。
- 未做双端 RTP 音频验收。

### 10.4 既有 S1/S2 验证

已完成的既有链路：

- 真实摄像头本地预览。
- muxPlayer 软件 OpenGL 出画。
- `session.hello`、双客户端真实房间、成员变化。
- P5 房间控制栏。
- 音频设备枚举探针。

### 10.5 尚未验证

以下不能标记为已验证：

- 两个真实客户端之间实际听到对方声音。
- 1v1 P2P ICE/DTLS 连通。
- 第三人加入后 SFU 音频切换。
- 静音/强制静音的服务端权威状态。
- 网络断开、恢复、重连。
- 音频设备占用/拔出。
- 长时间 join/leave 不泄漏。
- 真实远端视频渲染。

---

## 11. 已知问题和风险

### P0：影响当前音频验收

1. **真实双端音频未联调**
   - 目前只有 SDP 级测试。
   - 必须用两个客户端、不同 UID、同一房间验证。

2. **`room.mediaMode=sfu` 响应过早**
   - 当前在本地 publish offer 创建后返回成功。
   - 应等待 SFU subscribe offer/answer 完成，或在明确超时后返回。
   - 否则服务端可能认为切换已完成，但客户端接收链仍未建立。

3. **P2P candidate 空候选处理**
   - 当前 Empty candidate 被直接忽略。
   - 如果服务端需要 end-of-candidates 语义，需要补齐。

4. **P2P 连通状态依赖 `OnConnectionChange`**
   - 需要在真实 Windows 网络环境确认回调状态流转。
   - 需要确认 P2P connected 后 SFU 暂停不会造成短暂双声/静音。

5. **静音命令的 UI 乐观更新**
   - AppController 在服务端确认前先更新 UI。
   - 失败时 CAbi 目前只针对部分错误更新状态。
   - 需要把“请求态/确认态”分开，避免 UI 与服务端权威状态短暂不一致。

6. **音频初始化失败仍入房成功**
   - 当前 `media_enabled=false`，但房间仍可用。
   - 产品上需要决定是“静音入房”还是“拒绝入房”。

### P1：生产架构风险

1. `EngineCore` 仍然过大。
   - B1-B5 已移出命令/事件门面、信令运行时、会话生命周期、媒体编排和媒体管线。
   - 房间编排、媒体协调、P2P/SFU 切换、设备控制和统计仍在 `EngineCore`。
   - 建议下一步拆：
     - `SessionController`
     - `RoomController`
     - `MediaModeCoordinator`
     - `CommandRouter`
     - `CorrelationRegistry`
     - `GenerationGuard`

2. `SignalingClient` 公共头暴露 asio/websocketpp。
   - 应改为 PImpl，隐藏事件循环和 websocket 类型。

3. 未实现 WSS。
   - Engine 当前只接受 `ws://`。
   - 生产环境必须支持 `wss://`，并处理证书、代理、超时和关闭码。

4. 未实现完整重连/resume。
   - `session.hello` 没有 `resume.session_id`。
   - 信令关闭后当前实现直接重置媒体和房间。
   - B3 已加入会话 generation，旧回调不能污染新连接；resume 和重连策略仍未实现。

5. 没有服务端请求和本地操作的完整 correlation。
   - 会话级 generation 已覆盖信令、媒体初始化和异步回调。
   - 命令级 correlation、超时后的服务端确认态和媒体切换回滚仍未统一。

6. 设备热插拔未实现。
   - 默认设备变化、设备拔出、设备占用需要重新初始化 ADM 或回退。

7. `MediaSession` 没有 RTCStats 完整报告。
   - 生产诊断至少需要：
     - RTT
     - selected candidate pair
     - sent/received bytes/packets
     - packet loss/jitter
     - audio level
     - capture/playout underrun

### P2：功能缺口

1. 屏幕共享和 P2P 视频已实现；仍需补充异常恢复和多人切换验收。
2. `floor.request/release` 等发言权请求仍返回 501。
3. 多路视频池、9 路性能、5 秒无帧回收未验收。
4. 设置弹窗目前只是占位。
5. PTT/主持人审核模式下，本地采集门与服务端 floor 状态还需要完整联调。
6. 系统音 loopback 已接入；非默认播放端点和设备热切换仍需继续验收。
7. 安装包、DLL 依赖清单、签名和升级策略未实现。

---

## 12. 推荐后续开发顺序

### 阶段 A：完成真实音频验收

目标：1v1 P2P 和 3 人 SFU 都能双向听到声音。

工作项：

1. 启动两个客户端，使用不同 UID、相同房间。
2. 进入后确认：
   - `media.ready`
   - `audio.connection`
   - `p2p.state`
   - `media.stats.capture_frames > 0`
   - `media.stats.remote_tracks > 0`
3. 用第二个客户端验证麦克风开关。
4. 第三人加入，验证服务端触发 `room.mediaMode=sfu`。
5. 检查：
   - P2P 是否停止 SFU 发送但保持 PC。
   - SFU subscribe 是否收到远端音轨。
   - 是否出现双声或静音。
6. 修正完成后提交 S3.2。

音频验收标准：

- 1v1 双向音频。
- 第三人加入后持续音频。
- 静音/取消静音符合服务端确认。
- 收听静音只影响本地播放。
- 音量生效。
- 设备错误不崩溃。
- 关闭/离房不泄漏 PeerConnection 和 ADM。

### 阶段 B：协议和生命周期加固

- `session.hello` resume。
- WSS。
- 断线重连。
- 请求 correlation。
- generation guard。
- `room.mediaMode` 等待订阅完成。
- 服务端 `media.forceMute`、`floor.revoked` 状态一致性。
- `EngineCore` 拆分。

### 阶段 C：视频接入

目标：本地摄像头/远端视频/屏幕共享接入 WebRTC + muxPlayer。

工作项：

1. 摄像头 I420 采集源接入 WebRTC video source。
2. 建立 camera video track。
3. SFU publish 增加 video m-line。
4. 服务端 `sfu.offer` 的 consumers 映射到 `mid -> uid/source`。
5. 远端 WebRTC I420 帧通过 C ABI 传到 UI。
6. 远端视频注入 muxPlayer，而不是 QImage。
7. 屏幕共享 source。
8. 摄像头/屏幕 sender active 和 producer mute。
9. 5 秒无帧回收、paused producer、远端画面清理。

验收标准：

- 真实摄像头发布。
- 对端看到真实画面。
- 共享屏幕可发布/停止。
- 断流后画面及时清空。
- 不出现 WebRTC 解码后再编码。

### 阶段 D：生产化和发布

- 设备热插拔。
- 设置弹窗。
- 音频/视频诊断面板。
- 日志脱敏。
- crash dump。
- 安装包。
- 依赖签名。
- 版本升级。
- 网络异常矩阵。
- 按用户要求执行稳定性测试，不擅自长时间压测。

---

## 13. 联调操作建议

### 13.1 双客户端

建议使用两个不同工作目录或明确设置不同用户标识：

```text
client-a / room-demo / uid-a
client-b / room-demo / uid-b
```

如果同一机器运行两个 `rtc-talk.exe`，注意：

- 摄像头不能同时打开时，至少一个客户端使用纯音频。
- 麦克风和扬声器可能产生回声，必要时使用耳机。
- 不要用同一 Token/UID 模拟两个客户端。

### 13.2 观察点

在 Engine 中加入临时日志时优先观察：

- `media.ready` 的录音/播放设备名。
- `media.stats.capture_frames` 是否增长。
- `sfu_send_state` / `sfu_recv_state`。
- `p2p_state`。
- `remote_tracks`。
- `p2p.candidate` 是否有 mid/index。
- `room.mediaMode` 是否收到。
- `media.error` 的 code/detail。

不要把包含 Token、完整 SDP、ICE credential 的内容直接写入长期日志。

### 13.3 常见故障定位

| 现象 | 优先检查 |
|---|---|
| 入房成功但音频按钮不可用 | `media_enabled` 是否为 true |
| 能入房但听不到 | remote_tracks、sfu_recv_state、playout 设备 |
| 自己能听但对方听不到 | capture_frames、sfu_send_state、mute、ICE/RTP |
| P2P 不连通 | p2p offer/answer、candidate、ICE server、sfu_paused |
| 第三人加入后双声 | P2P connected 后 SFU 是否暂停 |
| 切 SFU 后无声 | `sfu.offer` answer、subscribe PC、sender active |
| Debug 崩溃 | ADM 是否在 worker 线程释放、iterator debugging 宏是否一致 |
| UI 卡顿 | 是否从 WebRTC 回调直接操作 QWidget 或大块复制 |

---

## 14. 交接检查清单

接手后先确认：

- [ ] 当前分支是 `rtc-talk-win`，不是 `main`。
- [ ] 旧工程 `rtc-talk` 没有改动。
- [ ] `git status` 干净，当前 HEAD 与 `origin/rtc-talk-win` 同步。
- [ ] `WEBRTC_SDK_DIR` 指向 M146 SDK。
- [ ] `QTDIR` 指向 Qt 5.15 msvc2019_64。
- [ ] `VCPKG_ROOT` 指向本地 vcpkg。
- [ ] Release 构建通过。
- [ ] Debug 构建通过。
- [ ] Release CTest 6/6（包含 `rtcwin_video_scene_tests`）。
- [ ] Debug CTest 6/6（包含 `rtcwin_video_scene_tests`）。
- [ ] fake 音频探针通过。
- [ ] 真实 wasapi 短链路通过。
- [ ] 双客户端真实音频联调完成后再宣布 S3.2 完成。
- [ ] 双客户端 SFU/P2P camera+screen 截图通过。
- [ ] 停止 camera/screen 后布局和画面清理正确。
- [ ] 便携包独立启动截图冒烟通过。
- [ ] 提交前检查 `git diff --check`。
- [ ] 只提交到 `rtc-talk-win` 分支，不碰 `main`。

---

## 15. 关键决策记录

### 为什么 libwebrtc 不复制进仓库

- SDK 约 2.3 GiB。
- 属于开源外部 SDK，不是用户封装源码。
- 通过 `WEBRTC_SDK_DIR` 允许开发环境注入。
- 产品发布时应把必要运行时 DLL/静态链接结果纳入安装产物，而不是提交整个 SDK。

### 为什么 muxPlayer 必须复制进仓库

- 它是用户封装并需要业务定制的媒体库。
- 不能依赖 `D:\work\device_tool` 当前工作树。
- 必须保留来源、版本、补丁和构建脚本。

### 为什么 UI 不链接 WebRTC

- ABI、运行库、异常模型冲突。
- UI 线程不能承受媒体同步阻塞。
- C ABI 是稳定隔离层。
- 远帧通过回调 + Qt queued invocation 传输。

### 为什么音频先使用 ADM

- 保留 WebRTC AEC/AGC/NS、抖动缓冲和设备主控。
- 避免自建音频时钟和回声消除。
- 系统音 loopback 是后续独立 adapter，不应污染麦克风主路径。

---

## 16. 文档和代码索引

重点文档：

```text
docs/build-and-test.md
docs/requirements.md
docs/architecture/01-system-context.md
docs/architecture/02-module-architecture.md
docs/architecture/03-domain-model.md
docs/architecture/04-engine-and-session.md
docs/architecture/05-media-architecture.md
plan/production-plan.md
```

重点代码：

```text
libs/webrtc_media/include/rtcwin/webrtc_media/media_session.h
libs/webrtc_media/src/media_session.cpp
libs/engine/include/rtcwin/engine/engine_core.h
libs/engine/src/engine_core.cpp
libs/client_gateway/src/c_abi_engine_client.cpp
libs/signaling/src/signaling_client.cpp
tests/audio_media_smoke.cpp
tests/signaling_smoke.cpp
```

本次交接结论：

> 当前代码已经从“仅有音频设备枚举”推进到“具备完整音频 PeerConnection 运行时和协议编排”，但仍必须完成真实双客户端音频联调，才能把 S3.2 标记为完成。后续最优先工作不是继续加视频，而是先把真实 P2P/SFU 音频闭环验收通过。

---

## 17. 接手架构 Review 与开发决策（2026-10-29）

接手人于 2026-10-29（`6ed604e` 提交后）完成一次独立架构 review，结论与裁决记录如下。

### 17.1 Review 结论

总体评价：**架构方向正确、边界纪律执行到位，可给“合理，带已知债务”的评价**。债务集中在 `EngineCore` 聚合与信令层健壮性，属可渐进偿还的实现债，不是结构性错误。

合理并应保持的部分：

1. C ABI DLL 边界解决真问题：libwebrtc ABI 要求（`/EHs-c-`、`/MT`、RTTI）与 Qt/MSVC 默认（`/EHsc`、`/MD`）在同一二进制内不可调和，DLL + C ABI 隔离是唯一正解；`media_session.h` PImpl 做到公共头零 libwebrtc 类型泄漏。
2. 线程模型：GUI / engine worker / signaling io / libwebrtc 三线程 + 所有回调 `PostToWorker` + 析构顺序 + ADM 线程亲和，事故高发区已全覆盖。
3. 三 PC 角色分离（`sfu-send` / `sfu-recv` / `p2p`）与 mediasoup send/recv 分离语义精确对齐；`Submit()` 命令队列把媒体操作串行化到 worker。
4. 探针式测试（`rtcwin_media_session_smoke` 等）可不碰 UI 验证媒体链路。

债务清单（按严重度）：

1. **`EngineCore` 上帝类（已解决）**：B4/B5 完成后
   `engine_core.cpp` 约 198 行，只保留线程、命令队列、模块装配和入口；
   会话生命周期已进入 `SessionController`，媒体域已进入
   `MediaOrchestrator`，MediaSession 内部已拆出
   `AudioPipeline`、`VideoPipeline`、`SdpNegotiator`、`MediaPeer`、
   `RemoteTrackRegistry`、`StatsCollector`。
2. **信令层未执行依赖隔离纪律**：`signaling_client.h` 公共头直接 `#include <asio.hpp>` 与 `<websocketpp/...>`，`webrtc_media` 做到了零泄漏而 signaling 没有，需 PImpl 化。
3. **协议健壮性缺口（部分解决）**（见 17.2 第 3 条裁决）：B3 已补齐会话 generation guard，旧信令、媒体和帧回调会被隔离；仍未实现 resume，断线仍整场重置；`mute_generation_` 仍只是局部保护；静音 UI 乐观更新未区分请求态/确认态（老工程 PTT 踩过同款坑）。
4. 该 review 完成时视频路径仅完成本地预览；S4.1 后续已补上 SFU 摄像头发布、consumer 映射和远端 I420 直注入 muxPlayer。屏幕共享与 P2P 视频仍是后续债务。

### 17.2 裁决与开发决策

1. **联调环境**：测试服可用，支持本机双开两个客户端实例（不同 UID 同房间）进行阶段 A 验收。
2. **拆分时机**：先完成阶段 A（真实双端音频验收），再拆 `EngineCore`（阶段 B）。拆分完成后必须重跑全部探针再继续。
3. **`ws://` 范围裁决**：仅使用 `ws://` 是因为当前为测试服务器，暂不建设 TLS；resume、generation guard、correlation、静音请求态/确认态分离等其他健壮性项照常列入优化计划。
4. **提交规范**：commit message 使用 `feat:` / `fix:` 等前缀，说明文字用中文。
5. **开发顺序**：维持 §12 的阶段 A → B → C → D，从阶段 A 开始。

---

## 18. 阶段 A 联调进度（2026-10-29 进行中）

本节记录接手后按 §17.2 决策启动的阶段 A（真实双端音频验收）进展，供中断后恢复。

### 18.1 已完成

| 项 | 结果 |
|---|---|
| 构建校验 | `6ed604e` 后源码与产物一致，增量重建通过 |
| 探针基线 | Release ctest 3/3 通过（`rtcwin_tests` / `rtcwin_signaling_tests` / `rtcwin_media_session_smoke`，fake ADM） |
| 真实信令冒烟 | `rtcwin_signaling_smoke` 对 `ws://47.117.159.193:8080/talk/v1/ws`：`hello ok → room.join ok mode=sfu → room.leave ok`；服务端 `healthz` 为 `4a24f30` / mediasoup 3.30.1 |
| 双开入房 | 本机双开 `rtc-talk.exe`（`rtcwin-stagea` / `rtcwin-stageb`，同房 `stagea-room`，auto 模式，WASAPI 后端） |
| P2P 切换 | 干净房间 2 人 auto：服务端正确切 P2P（B 的 join 应答携带 `media_mode:"p2p"`、`p2p.started`）；双方 `pc:p2p` 均 `connected` |
| SFU 暂停语义 | P2P connected 后 `sfu_paused:true` 且 sfu-send PC 保持 `connected`（暂停发送但保留 PC，符合 §12 阶段 A 检查项） |
| 远端轨 | A `remote_tracks:1`、B `remote_tracks:2`（B 多一条 sfu-recv 轨，来源待查，见 18.3） |
| UI 静音切换 | 通过 UI 麦克风按钮开麦成功：双方收到 `media.muted muted:false`，日志确认 UI 命令链路（按钮 → AppController → `media.setMute`）工作正常 |
| UI 状态 | 房间页显示 `P2P` 芯片、`2 人`、计时正常；截图 `build/screenshots/dual-1.png` / `dual-2.png` |

### 18.2 联调工具与新增改动（未提交）

1. `apps/rtc-talk/main.cpp`：auto-join 新增 `--auto-join-backend`（wasapi|fake|none）与 `--auto-join-media-mode`（auto|sfu|p2p）。此前 auto-join 硬编码 `AudioBackend::None`，无法做真实音频联调。默认值保持原行为。
2. `libs/client_gateway/src/c_abi_engine_client.cpp`：新增 `RTCWIN_TRACE_EVENTS` 环境变量开关——开启后在 stderr 打印全部引擎事件与 Submit 命令，并每 5 秒自动提交一次 `media.stats`（联调观察 `capture_frames` / `remote_tracks` / `p2p_state`）。事件信封不含 token/SDP/ICE 凭证，符合 §13.2 红线。
3. 启动方式（PowerShell，GUI 子系统需强制 stderr）：

```powershell
$env:RTCWIN_TRACE_EVENTS = '1'
$env:QT_FORCE_STDERR_LOGGING = '1'
$env:PATH = "C:\Qt\5.15.2\msvc2019_64\bin;" + $env:PATH
.\build\vcpkg-windows-x64\bin\Release\rtc-talk.exe `
  --auto-join-url ws://47.117.159.193:8080/talk/v1/ws `
  --auto-join-room stagea-room --auto-join-uid rtcwin-stagea `
  --auto-join-dev-token --auto-join-backend wasapi `
  --auto-join-media-mode auto *> build\logs\stagea-a.log
```

### 18.3 发现的问题

1. **`/dev/token` 返回 JSON 包装**：接口返回 `{"token":"...","uid":...,"exp":...}`，直接整串当 token 用会被 401 拒绝；必须提取 `token` 字段。UI 的 `fetch_dev_token` 路径不受影响（Gateway 自己解析）。
2. **服务端会话残留约 40 秒**：客户端被杀后 `healthz` 的 `sessions`/`rooms` 并不立即清零（约 40s 超时）。快速重启客户端会用旧会话污染房间模式判定——首次双开实验因此没有切 P2P（观察到 `room.peerLeft reason:"replaced"`，媒体停在 SFU）。**联调规程：杀掉客户端后必须轮询 `healthz` 直到 `sessions:0, rooms:0` 再重开**。
3. **B 端 `remote_tracks:2`**：P2P 模式下 B 同时持有 p2p 轨与 sfu-recv 轨（sfu-recv 也显示 connected）。A 的 SFU producer 已暂停所以理论上无声，但"入房即 P2P 的成员为何建立 sfu-recv"需要查 `FinalizeJoin` / `StartMediaForCurrentMode` 的分支；这也是双声风险的候选来源。
4. **当前卡点：开麦后 `capture_frames` 仍为 0**。双方 `muted:false`、`p2p_state:connected`、`remote_tracks>0`，但 `media.stats.capture_frames` 停在 0——采集侧（ADM 起录 / CaptureGate 计数）疑似未真正跑起来，正待排查。对照 §13.3："自己能听但对方听不到 → 优先查 capture_frames"。

### 18.4 下一步

1. 排查 `capture_frames=0`：先跑 `rtcwin_audio_devices_probe` 确认设备，再对照 `MediaSession::Initialize` 的 `InitRecording/StartRecording` 路径与 CaptureGate 计数位置（含 WASAPI 后端下 `SetMuted(false)` 是否真正走到 ADM 起录）。
2. 查 B 端 sfu-recv 轨来源（18.3.3）。
3. capture_frames>0 后：真人听音确认双向有声，再做第三人加入切 SFU 的场景。
4. 全部通过后按 §17.2 提交规范提交阶段 A 改动（含 18.2 的两个联调工具改动）。

### 18.5 阶段 A 修复与验证结果（2026-10-29）

18.3 的阻塞项已经定位并修复。根因不是麦克风硬件或系统权限：

- FFmpeg DirectShow 对同一麦克风连续采集 2 秒成功。
- WebRTC 内部日志显示 ADM 在 `AudioTransport` 注册前就启动 playout，持续输出 `Invalid audio transport`。
- 该错误导致 ADM 虽然 `Recording()==true`，但 PCM 从未进入 VoiceEngine，表现为 `capture_frames=0`、RTP 包数为 0。

修复内容：

1. ADM playout 不再在 `MediaSession::Initialize` 中提前启动。现在在 SFU/P2P PeerConnection 创建后、添加音轨前启动，与已验证的旧实现顺序一致。
2. fake ADM 不要求启动物理 playout。
3. 恢复 `CaptureGate`，在编码前执行静音门并统计真实 10ms 采集帧。
4. 增加 `RTCWIN_WEBRTC_LOG=1` 联调开关，默认关闭。
5. 补充标准 ICE 状态回调。M146 下 `OnConnectionChange` 未及时从 `new` 变为 `connected`，改为同时消费 `OnStandardizedIceConnectionChange` 并去重上报。
6. 恢复远端音轨按 `pc + mid` 去重，P2P 关闭时释放对应远端轨引用。
7. trace 日志对 `token`、`authorization`、`credential`、`password`、`sdp`、`candidate` 做递归脱敏；`RTCWIN_TRACE_EVENTS` 严格只接受 `1/true/yes/on`。
8. `none` 音频后端贯通到 `room.join.media.send=false`，不再被静默映射为 WASAPI。
9. `room.mediaMode=sfu` 改为等待 SFU publish 和 subscribe 都就绪后再关闭 P2P；5 秒未就绪则拒绝切换并保留 P2P。

短链路验证结果：

| 场景 | 结果 |
|---|---|
| fake 探针 | CTest 3/3 通过 |
| 真实 WASAPI | ADM 初始化成功，录音/播放设备正确 |
| 1v1 P2P | 双方 P2P connected；A `p2p_audio_packets_sent=245`、`audio_packets_received=44`；B 双向 RTP 同步增长 |
| 采集帧 | 取消静音后 A `capture_frames=490 -> 990`，B `capture_frames=292`，确认真实麦克风进入 WebRTC |
| 静音 | A 上行包 `246 -> 253` 后停止增长；对端收到的 A 包数停在 253；`capture_frames` 同步停止 |
| 第三人加入 | P2P 关闭后三方均进入 SFU；`sfu-send/sfu-recv connected`；远端音轨 2 路；发送与接收 RTP 持续增长 |
| trace 安全 | 日志中 `token` 显示为 `<redacted>`，未发现 JWT、完整 SDP 或 ICE candidate |

当前仍需完成：

1. 收听静音和音量端到端人工听音验收。
2. 设备占用、设备拔出和默认设备变化。
3. 离房/重进、断线重连和 P2P 失败回退的重复验证。
4. 以上通过后关闭 S3.2 阶段 A。

### 18.6 提交记录

阶段 A 音频修复和系统音采集已提交并推送：

```text
3b79703 fix: 打通真实音频并完善P2P/SFU切换
5ab73fa feat: 接入WASAPI系统音采集
```

验证提交包含 Release/Debug 全量构建、CTest 3/3、1v1 P2P、三人 SFU、
静音停发和 trace 脱敏检查。

### 18.7 系统音采集验证（2026-10-29）

已新增 WASAPI shared-loopback 音频源：

- `LoopbackAudioCapture` 采集默认播放端点，支持 float32/PCM16/PCM32。
- 自动转换为 48 kHz、单声道、16-bit，并按 10 ms 通过
  `AudioTransport::RecordedDataIsAvailable` 送入 WebRTC。
- `SwitchableAudioDevice` 使用 Core Audio ADM 负责播放、设备管理和麦克风路径；
  系统音模式只替换录音源。
- 系统音模式关闭 3A，避免对音乐/会议系统音做降噪、AGC 和回声消除。
- SFU track source 上报为 `screen`，麦克风仍为 `mic`。
- 默认播放设备变化时，loopback 在采集线程自动重开。

短链路验证：

| 项 | 结果 |
|---|---|
| A 端系统音会话 | `recording_device=system-default-render` |
| A 端采集 | `capture_frames=491 -> 992` |
| A 端 P2P 上行 | `p2p_audio_packets_sent=245 -> 499` |
| B 端接收 | `audio_packets_received` 持续增长 |
| B 端听音电平 | 测试音期间 `remote_audio_level=0.001 -> 0.5719` |
| 退出 | 正常关闭，无重复 Terminate 崩溃 |

当前限制：

1. 只跟随 Windows 默认播放设备，尚未提供独立“系统音播放端点”选择。
2. 运行时在麦克风/系统音之间切换尚未暴露 C ABI；当前在入房前选择。
3. 非默认端点选择、设备热插拔和独占模式应用兼容性仍需继续测。

### 18.8 S4.1 摄像头 SFU 与远端 muxPlayer（2026-10-29）

本阶段只关闭“摄像头 SFU 发布 + 远端显示”，不把屏幕共享和 P2P 视频计为完成。

实现结构：

```text
Media Foundation camera
  -> camera_win I420
  -> Engine OnCameraFrame
  -> MediaSession::PushCameraFrame
  -> CameraVideoTrackSource
  -> MF H.264 encoder
  -> sfu-send PeerConnection
  -> mediasoup
  -> sfu-recv PeerConnection
  -> RemoteVideoSink
  -> C ABI v2 remote frame
  -> CAbiEngineClient latest-frame-per-stream
  -> MuxVideoSurface
  -> VideoTile
```

关键改动：

1. `AudioMediaSession` 重命名为 `MediaSession`，音频与摄像头共用一个 SFU send
   PeerConnection。视频不能另建 PC，否则 mediasoup send transport 的 DTLS
   fingerprint/ICE ufrag 无法同时绑定两个 PeerConnection。
2. 新增 `CameraVideoTrackSource`。摄像头时间戳在媒体边界统一使用
   `webrtc::TimeMicros()`；MF device timestamp 可能重复或回退，曾导致编码器
   不出包。
3. 新增 Media Foundation H.264 encoder/decoder factory，只声明
   `H264/90000`、`packetization-mode=1`、`profile-level-id=42e01f`。
4. `sfu.publish.tracks` 从仅音频扩展为 `audio + camera`；camera producer 初始
   paused，开启摄像头后发送 `media.setMute{source:"camera",muted:false}`。
5. 解析 `sfu.offer.consumers[]` 的 `mid/uid/kind/source`，作为远端视频帧的
   路由元数据。
6. C ABI 版本从 1 升到 2，`rtcwin_frame_view` 新增 `uid`。Engine DLL 与
   `rtc-talk.exe` 必须同时更新。
7. 房间 `VideoTile` 改用 `MuxVideoSurface`，远端视频不再走 QImage 生产渲染。
8. `log-cpp` 初始化增加失败降级：日志目录不可写时回退 console sink，不允许
   因日志初始化失败阻断 Engine 创建。

验证结果：

| 项 | 结果 |
|---|---|
| Release 全量构建 | 通过 |
| Debug 全量构建 | 通过 |
| Release CTest（B3 后） | 5/5 通过 |
| Debug CTest（B3 后） | 5/5 通过 |
| `rtcwin_media_session_smoke` | SDP 含 `m=audio`、`m=video`、`H264/90000`；合成 I420 推帧成功 |
| 发送端摄像头采集 | `camera_frames=31 -> 179` |
| 发送端视频上行 | `video_packets_sent=14 -> 293` |
| 接收端视频下行 | `video_packets_received=53 -> 325` |
| 接收端截图 | 远端摄像头画面正常显示在 muxPlayer surface |

证据文件：

```text
build/windows-x64/s4-smoke2/a-view.png
build/windows-x64/s4-smoke2/b-view.png
build/windows-x64/s4-smoke2/a-engine.log
build/windows-x64/s4-smoke2/b-engine.log
```

仍未完成：

1. 屏幕共享采集、发布、订阅和 paused 清屏。
2. P2P 视频。
3. 多路视频 session pool、9 路性能、5 秒无帧回收。
4. 摄像头设备切换、离房重进、异常恢复的重复验收。
5. 当前视频 tile 每路一个 GL surface；多路规模扩大前需要 session pool。
6. 当前摄像头 video m-line 在首次 SFU publish 时预创建并保持 producer paused，
   会占用服务端 `video_max_publishers` 的“新视频轨”判断名额。若房间策略允许的
   发布者数小于成员数，后续需要改为首次开摄像头时再重协商添加 video m-line。

### 18.9 EngineCore 生产化拆分（B1-B5 已完成）

当前体量：

| 文件 | 行数 | 问题 |
|---|---:|---|
| `libs/engine/src/engine_core.cpp` | 198 | 线程、命令队列、模块装配和 C ABI 入口 |
| `libs/engine/src/session_controller.cpp` | 445 | 连接、hello、入房、离房、心跳和信令回调 |
| `libs/engine/src/media_orchestrator.cpp` | 1248 | 摄像头、媒体命令、SFU/P2P 协商和媒体事件 |
| `libs/webrtc_media/src/media_session.cpp` | 636 | MediaSession 公共 PImpl、Peer 组合和生命周期 |
| `libs/webrtc_media/src/audio_pipeline.cpp` | 386 | ADM、设备、采集、播放、静音和音量 |
| `libs/webrtc_media/src/video_pipeline.cpp` | 60 | 摄像头视频源、视频轨和帧推送 |
| `libs/webrtc_media/src/media_peer.cpp` | 469 | PeerConnection 生命周期和媒体轨控制 |
| `libs/webrtc_media/src/remote_track_registry.cpp` | 214 | 远端音频/视频轨、sink 和启用状态 |
| `libs/webrtc_media/src/sdp_negotiator.cpp` | 193 | offer/answer 创建、设置和超时 |
| `libs/webrtc_media/src/peer_stats_collector.cpp` | 92 | RTCStats 采集和聚合 |
| `libs/client_gateway/src/c_abi_engine_client.cpp` | 872 | C ABI 解析、HTTP、帧邮箱和快照更新集中 |

拆分顺序：

1. **B1 Command/Event Facade（已完成）**：抽出 `EngineCommandRouter` 与
   `EngineEventSink`。C ABI 命令统一由 nlohmann JSON 解析，事件信封统一由
   nlohmann JSON 构造，已删除 `EngineCore` 内手拼 JSON、`EscapeJson` 和
   `ParseIntField`。
2. **B2 Signaling Transport（已完成）**：抽出 `SignalingTransport`，由其独占
   `asio::io_context`、信令线程、`SignalingClient`、连接/请求/通知/发送和停止
   生命周期。EngineCore 不再直接持有 `signaling_io_`、`signaling_thread_`、
   `signaling_work_` 或 `signaling_` 指针。
3. **B3 Session State Machine（已完成）**：新增 `SessionStateMachine`，用显式
   `SessionPhase` / `SessionMediaMode` 和 generation 表达 connecting、
   signaling-ready、joining、joined、leaving、failed；join command 与 generation
   绑定，信令、媒体事件、远端帧和定时器回调均先校验 generation。
   已删除 `EngineCore` 内的 `session_ready_`、`room_joined_`、
   `media_enabled_`、`media_mode_` 和独立 `session_command_id_`。
4. **B4 Media Orchestrator（已完成）**：发布/订阅、P2P 到 SFU 切换、
   camera/screen 控制、媒体命令和统计已从 EngineCore 迁出。新增
   `SessionController`，EngineCore 仅保留线程、命令路由和模块装配。
5. **B5 MediaSession 内聚拆分（已完成）**：已拆出 `AudioPipeline`、
   `VideoPipeline`、`SdpNegotiator`、`MediaPeer`、
   `RemoteTrackRegistry`、`StatsCollector`；MediaSession 只负责组合和
   对外 API。
6. 每步只做结构迁移，行为不变；每步后跑 Release/Debug CTest、短双端
   camera smoke，并保持 C ABI 不破坏。

目标：

- `engine_core.cpp` 已降到 198 行，只保留装配、线程和命令入口。
- 不允许在 `EngineCore` 内直接拼 JSON 或处理 SDP。
- P2P/SFU 切换、摄像头控制不再依赖隐式 bool 时序。

当前验证：

- Release/Debug 全量构建通过。
- Release/Debug CTest 5/5（新增 `rtcwin_session_state_tests`）。
- B5 双客户端 SFU 摄像头回归通过：A `camera_frames=15 -> 89`、
  `video_packets_sent=0 -> 491`；B `video_packets_received=57`；
  两端 `room.leave ok=true`。
- B5 真实 WASAPI 双端回归通过：双方 `recording=true`，取消静音后
  `capture_frames` 增长，静音后停发；远端收到音频包并读到音平。
- B3 双客户端短链路通过：A `camera_frames=27 -> 140`、
  `video_packets_sent=15 -> 246`；B `video_packets_received=100`；
  两端 `room.leave ok=true`。
- B3 证据目录：`build/windows-x64/b3-smoke-20261029-220755`。
- B2 后真实双端 SFU 摄像头回归通过：发送端 `video_packets_sent=40 -> 760`，
  接收端 `video_packets_received=142 -> 861`。

### 18.10 S4.2 屏幕共享与 P2P 视频增量（2026-10-30）

已完成：

- `DesktopVideoSource`：WGC/DXGI 屏幕采集、最多 1920×1080、15fps、
  BGRA 缩放和 I420 转换。
- `VideoPipeline` 同时管理 camera/screen 视频源和两路视频轨。
- SFU publish 预创建 audio/camera/screen 三条 m-line，并携带
  `source=screen` producer。
- `media.toggleScreenShare` 已实现：服务端确认后启动采集，停止时先停采并发送
  `media.setMute{source:"screen", muted:true}`。
- P2P 预创建 audio/camera/screen 三条 m-line；双方 `caps.p2p_video=true` 时
  摄像头和屏幕共享均可直连。
- P2P 远端 UID 映射和 video pause 作用域已修正；`sfu_paused` 不再禁用 P2P 视频轨。
- 自动联调参数新增 `--auto-screen-ms`、`--auto-screen-stop-ms`。

证据：

```text
build/windows-x64/screen-smoke-20261030-122443/b-screen.png
build/windows-x64/screen-stop-smoke-20261030-122528/b-after-stop.png
build/windows-x64/p2p-video-fix2-20261030-123224/b-p2p-video.png
build/windows-x64/p2p-screen-20261030-123251/b-p2p-screen.png
```

仍未完成：

- 多路 muxPlayer session pool。
- 9 路视频性能验收。
- 5 秒无帧回收与异常断流 UI 清理。
- 多屏选择和窗口共享选择。

M1 的双路呈现、共享 PIP 和停止 camera/screen 已完成，详见 §18.12。

### 18.11 旧客户端功能差异审计（2026-10-30）

已对齐：

- 麦克风、摄像头、屏幕共享、收听、音量命令。
- SFU/P2P 摄像头与屏幕共享媒体传输。
- `media.forceMute` 按 mic/camera/screen 分流。
- P5 预入会设备枚举、本地摄像头预览和房间基础控制栏。

仍缺失或只完成一部分：

| 功能 | 当前状态 | 缺口 |
|---|---|---|
| 同成员 camera + screen 联合显示 | M1 已实现 | SFU/P2P 双端截图已通过 |
| 共享主画面 + 摄像头 PIP | M1 已实现 | P5 布局截图已通过，待用户阶段确认 |
| 本地/1v1 画中画 | 部分 | Shared PIP 已完成；独立 1v1 页面仍待确认 |
| camera/screen 独立 Tile | M1 已实现 | 使用 `uid + source` 作为 Tile 键 |
| 多路 muxPlayer session pool | 未实现 | 仍是一路一个 Surface |
| 5 秒无帧回收 | 未实现 | `VideoStreamState.producer_paused/last_frame_ms` 未驱动 UI 清理 |
| 9 路视频性能 | 未验收 | 无多人多路压力证据 |
| `floor.request/release` | 返回 501 | Engine 未注册命令 |
| PTT/moderated 本地采集门 | 未闭环 | 未按 floor 权威状态开闭麦克风 |
| 主持人授权/全员静音/踢人/策略 | 未实现 | UI 有信号/菜单，Engine 无 host.* 命令且未连接 |
| `room.subscribeLevels` | 未实现 | 成员音量条没有服务端实时 level 来源 |
| `floor.revoked` 一致性 | 未实现 | 被收回发言权后的本地状态回滚缺失 |
| 房间设置弹窗 | 占位 | 当前只显示提示文字 |
| 房间设备面板 | 占位 | 显示“等待媒体引擎枚举设备” |
| 房间连接/媒体状态面板 | 占位 | 显示“等待媒体统计接入” |
| 会议中设备切换 | 未实现 | 仅预入会选择设备 |
| 设备热插拔/拔出/占用恢复 | 未实现 | 无设备变化监听和自动回退 |
| 多屏/窗口共享选择 | 未实现 | 当前固定第一个屏幕 |
| 非默认系统音端点选择 | 未实现 | 当前 loopback 仅默认播放端 |
| resume/reconnect | 未实现 | 无 session_id 恢复、退避和过期处理 |
| `session.refreshIce` / `sfu.restartIce` | 未实现 | 无 TURN 凭证刷新和 ICE restart |
| P2P 超时及失败回退 | 部分 | SFU 切换有 5 秒保护，P2P 失败链未完整 |
| 完整 RTCStats | 部分 | 有包/字节/音平，无 RTT、jitter、loss、码率、candidate pair |
| WSS/TLS | 未实现 | Engine 只接受 `ws://` |
| Token 安全持久化 | 未实现 | 仅内存输入，无 Credential Manager |
| 安装/签名/升级 | 未实现 | 当前只有便携 ZIP |
| 核心功能自动回归 | 部分 | 无双路联合、PTT/host、热插拔、resume 自动测试 |

明确不在当前一期范围：

- `call.invite` 呼叫弹窗；一期按需求直接入房。
- H.265 P2P、simulcast 和多层订阅；属于后续能力。

### 18.12 M1 视频场景与双路呈现（2026-10-30）

本轮生产化结构变更：

- application 层新增 `VideoSceneBuilder` 和 `VideoScene` 强类型模型，负责把
  `SessionSnapshot` 收敛为 Grid/Focus/Shared 场景。
- UI 层新增纯布局策略 `VideoLayoutEngine`，将场景转换为 Grid 布局计划，
  不在页面类中散落行列判断。
- `VideoGrid` 以 `uid + source` 管理 Tile，同一成员的 camera 和 screen
  可以同时存在。
- Shared 场景的 screen Tile 内嵌独立 `MuxVideoSurface` 作为 camera PIP；
  PIP 使用等比裁切、圆角裁切、边框和“摄像头”标签。
- Shared 布局已对齐 P5：主画面占满上方三行，其他成员位于底部成员胶片条。
- `CAbiEngineClient` 的本地帧缓存从单槽改为按 `uid + source` 的多流缓存，
  本地 camera/screen 不会互相覆盖；本机预览允许空 UID 帧。
- 修复帧缓存路由缺陷：此前把帧从局部变量移入缓存后重新按 key 查询，可能得到
  空帧，导致远端帧已经到达 `VideoGrid` 但 Tile 永远显示占位。现在缓存对象
  直接传给 Tile，并避免依赖函数参数求值顺序。
- `MuxVideoSurface` 改为 `KeepAspectRatioByExpanding`，保持 P5 的铺满效果，
  同时避免视频被拉伸。

新增/调整文件：

```text
libs/application/include/rtcwin/application/video_scene.h
libs/application/src/video_scene.cpp
libs/ui/include/rtcwin/ui/video/video_layout.h
libs/ui/src/video/video_layout.cpp
libs/ui/src/video/video_grid.cpp
libs/ui/src/video/video_tile.cpp
libs/client_gateway/src/c_abi_engine_client.cpp
```

验证：

- Release/Debug 构建通过。
- CTest 6/6 通过，新增 `rtcwin_video_scene_tests`。
- P5 本地摄像头预览截图：
  `build/windows-x64/m1-camera-preview-cover.png`。
- 仅屏幕共享远端截图：
  `build/windows-x64/m1-route-fixed-20261030-131603/b.png`。
- camera+screen 双路远端截图：
  `build/windows-x64/m1-dual-final-20261030-131626/b.png`；主画面为共享屏幕，
  右下角为对讲人摄像头 PIP。
- P2P camera+screen 双路远端截图：
  `build/windows-x64/m1-p2p-p5-20261030-132435/b.png`。
- 停止屏幕共享后自动回到 camera Grid：
  `build/windows-x64/m1-stop-p5-20261030-132505/b-after-stop.png`。
- 停止摄像头后共享屏幕保留、PIP 消失：
  `build/windows-x64/m1-stop-camera-20261030-132838/b-after-camera-stop.png`。

注意：M1 的 SFU/P2P 双端截图、停止 screen 回退、停止 camera 保留 screen
均已验证。M2 的 `VideoSurfacePool`、`FrameHealthMonitor`、5 秒无帧回收
尚未开始。

便携包（包含 M1 双路视频、P5 Shared 布局与 PIP）：

```text
D:\google\webrtc-win-client\rtc-talk-win\dist\rtc-talk-win-20261030-132934.zip
SHA256: A33A41A3537B620BDED829E54714F38854B2AFEE3203E176762B71BF7BE878B7
```

包内 `VERSION.txt` 的提交号为 `f2cc1a5`。旧的
`rtc-talk-win-20261030-132716.zip` 仅保留为 M1 早期包，不作为当前交付包。

打包后的 `rtc-talk.exe` 已通过 1440x900 启动截图冒烟，截图位于
`build/windows-x64/package/package-smoke-m1.png`。
