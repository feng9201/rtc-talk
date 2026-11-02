# rtc-talk-win

Windows x64 生产级 RTC 桌面客户端，面向实时对讲、多人音视频会议和屏幕共享场景。

本工程不是 `client-win` demo 的换皮版本，也不复用 demo 的耦合类结构。
`client-win` 只作为协议、行为和功能样本；产品代码在本工程重新划分领域、
应用、Engine、媒体、C-ABI、Qt UI 和平台适配层。

## 1. 项目目的

`rtc-talk-win` 的目标是交付一个可长期维护、可诊断、可测试、可发布的 Windows
桌面客户端，满足以下核心场景：

- 1v1 对讲和 P2P 音视频。
- 多人房间 SFU 音视频。
- 摄像头、屏幕共享和系统音采集。
- 自由发言、PTT、主持人审核三种对讲模式。
- 主持人授予/收回发言权、全员静音、踢人和房间策略修改。
- 断线重连、resume、ICE 恢复和媒体状态隔离。
- 多路远端视频宫格、聚焦、共享主画面和摄像头 PIP。
- 设备切换、热插拔回退、状态页和脱敏诊断包。
- WSS/TLS、JWT、Windows Credential Manager 等安全和发布能力。

一期明确不包括：

- macOS、Linux、移动端和 RK 客户端。
- 录制、GB28181、E2EE、Simulcast。
- 呼叫邀请弹窗、自动更新服务、多节点 SFU 调度。
- 超出当前服务端协议的新媒体能力。

## 2. 当前状态

| 能力 | 状态 |
|---|---|
| M1 视频场景、摄像头/屏幕双路、共享 PIP | 已完成 |
| M2 多 Surface、帧健康和无帧回收 | 已完成 |
| M3 Floor/PTT、主持人和实时音量 | 已完成 |
| M4 设备选择、热插拔回退、屏幕/窗口源 | 已完成 |
| M5 重连、resume、ICE refresh/restart | 已完成 |
| M6 RTCStats、状态页和脱敏诊断包 | 已完成 |
| 多人 SFU 视频稳定性修复 | 已完成 |
| M7 客户端 WSS、系统证书校验路径、Credential Manager | 已完成 |
| M7 正式域名证书联调、正式认证刷新 | 暂停 |
| S8 安装包、签名、升级、回滚和干净机验收 | 暂停 |
| 30 分钟/2 小时长稳、强制 TURN 和完整弱网验收 | 未执行 |

当前结论：

```text
功能联调版本可用，生产发布未就绪。
```

## 3. 需求边界

### 3.1 功能要求

#### 会话与信令

- 支持 `ws://` 开发环境和 `wss://` 正式环境。
- 支持 JWT 身份认证；开发环境可使用 `/dev/token`。
- 支持 `session.hello`、`room.join`、`room.leave`、心跳和成员快照。
- 主动离房、网络断开、被踢、账号替换必须有明确分流。
- 断线后支持退避重连、resume、恢复过期处理和 ICE 恢复。
- 所有异步回调必须带 generation，旧会话不能污染新会话。

#### 入房与设备

- 支持信令地址、UID、Room、Token、设备、音频后端、媒体模式和对讲模式配置。
- 支持摄像头、麦克风、播放设备、系统音端点和屏幕/窗口源枚举。
- 支持入房前摄像头预览，预览生命周期不得影响正式发布。
- 设备被占用、权限拒绝、拔插和切换失败不能导致客户端退出。
- 摄像头、音频和屏幕采集不得阻塞 Qt UI 线程。

#### 音视频

- 支持摄像头、屏幕共享和系统音采集。
- 支持 1v1 P2P、多人 SFU 和房间模式切换。
- 支持摄像头、屏幕共享、系统音和远端音频的静音/音量控制。
- 远端视频按 `UID + source` 独立路由，不允许不同成员串流。
- 每路视频只保留最新帧；UI 合并刷新，不允许旧帧无限堆积。
- 支持 9 路远端 Surface 上限、共享主画面、PIP 和胶片条。
- 无麦克风时允许降级为视频模式，不能阻断 SFU 视频接收。

#### 主持人与诊断

- 支持自由发言、PTT、moderated 模式。
- 支持发言权申请、排队、取消、释放和 revoke。
- 支持主持人授权、收回、全员静音、踢人和策略局部更新。
- 状态页显示连接、媒体模式、RTT、抖动、丢包、码率和候选对。
- 日志按大小轮转，Token、密码、SDP、ICE candidate 脱敏。
- 支持导出脱敏诊断 ZIP。

### 3.2 非功能要求

- Windows 10 22H2、Windows 11 x64。
- Qt 5.15.2 Widgets，基准窗口 1440x900，最小窗口 1100x720。
- UI 主线程单次事件处理目标小于 16 ms。
- 视频 UI 刷新目标合并到约 30 fps。
- 远端 Surface 最多 9 路，单路仅保留最新帧。
- UI 使用 `/MD`，媒体核心使用 `/MT`，只能通过版本化 C ABI 交互。
- 关键资源使用 RAII，关闭顺序可重复执行。
- 每阶段必须通过 Release/Debug 构建、CTest 和真实服务短链路验证。

## 4. 总体架构

```text
┌────────────────────────────────────────────────────────────────────┐
│ rtc-talk.exe                                             /MD      │
│                                                                    │
│ Qt Widgets UI                                                      │
│   MainWindow / PrejoinPage / RoomPage / DevicePanel                │
│   VideoGrid / VideoLayout / muxPlayer Surface Pool                 │
│                 │                                                  │
│ Application                                                       │
│   AppController / FloorController / HostController                 │
│   VideoSceneBuilder / FrameHealthMonitor / SettingsStore            │
│                 │                                                  │
│ Client Gateway                                                     │
│   CAbiEngineClient / event decoding / frame coalescing             │
└─────────────────┼──────────────────────────────────────────────────┘
                  │ rtcwin_client_api.h，C ABI v2
┌─────────────────▼──────────────────────────────────────────────────┐
│ rtcwin_engine.dll                                         /MT      │
│                                                                    │
│ EngineCore                                                        │
│   CommandRouter / EventSink / generation guard                     │
│                                                                    │
│ SessionController        MediaOrchestrator                         │
│   SignalingTransport       MediaSession                            │
│   ReconnectController      SFU send / SFU recv / P2P               │
│                            camera_win / WASAPI / WGC / libwebrtc   │
└────────────────────────────────────────────────────────────────────┘
```

### 4.1 进程和 ABI 边界

- `rtc-talk.exe` 承载 Qt UI、应用编排和 C ABI 客户端。
- `rtcwin_engine.dll` 承载信令、会话、媒体和 Windows 采集能力。
- `rtcwin_log_adapter.dll` 隔离日志库 CRT/ABI 差异。
- C ABI 使用版本化命令/事件信封和 I420 帧视图。
- UI 不直接包含 libwebrtc、Media Foundation、WASAPI、asio 私有头文件。
- `rtc-talk.exe` 与 `rtcwin_engine.dll` 是兼容对，不能只替换其中一个。

### 4.2 线程模型

| 线程 | 主要职责 |
|---|---|
| Qt UI 线程 | 页面、模型、布局、muxPlayer Surface 提交 |
| Engine worker | 命令串行执行、状态机、媒体编排、事件生成 |
| Signaling asio 线程 | WebSocket 生命周期、RPC、心跳、重连 |
| WinHTTP WSS worker | WSS 握手、SChannel 校验、收发循环 |
| libwebrtc 线程 | network / worker / signaling、ICE、DTLS、RTP、编码解码 |
| 采集线程 | Media Foundation 摄像头、WASAPI、WGC/DXGI |
| muxPlayer GL 线程 | 单 Surface 的 YUV 纹理上传和 OpenGL 绘制 |

跨线程边界只传递拷贝后的强类型快照、事件和帧视图。旧 generation 的异步回调
必须丢弃。

## 5. 模块职责

| 目录/target | 职责 |
|---|---|
| `apps/rtc-talk` | 进程入口、窗口启动、CLI 联调参数 |
| `libs/domain` | 强类型领域模型、快照、命令/事件相关基础类型 |
| `libs/application` | 应用编排、设置、凭证、诊断、Floor/Host、视频场景和帧健康 |
| `libs/client_api` | 对外 C ABI 接口定义 |
| `libs/client_gateway` | Qt 到 Engine 的 C ABI 客户端、事件解码、帧合并 |
| `libs/engine` | EngineCore、SessionController、MediaOrchestrator、重连和状态机 |
| `libs/signaling` | WebSocket、JWT handshake、RPC、超时、心跳、WinHTTP WSS |
| `libs/webrtc_media` | AudioPipeline、VideoPipeline、PeerConnection、SDP、远端轨道和 RTCStats |
| `libs/camera_win` | Media Foundation 摄像头枚举和采集 |
| `libs/muxplayer_adapter` | muxPlayer Surface 封装和 Raw I420 提交 |
| `libs/ui` | P5 Qt Widgets 页面、控件、布局和视频网格 |
| `libs/log_adapter` | log-cpp 的 C ABI 隔离适配器 |
| `third_party/muxplayer` | 用户维护的 muxPlayer 源码与依赖 |

详细模块边界见 [docs/architecture](docs/architecture/README.md)。

## 6. 信令与会话链路

```text
Prejoin
  -> Token Credential Manager / dev token / HTTPS token endpoint
  -> WebSocket ws/wss + Authorization: Bearer <JWT>
  -> session.hello
  -> room.join
  -> media.ready
  -> SFU publish / SFU offer answer / P2P setup
  -> room / speaker / floor / host / media events
```

关键约束：

- WebSocket 地址支持 `ws://` 和 `wss://`。
- WSS 使用 Windows WinHTTP + SChannel，使用系统证书库校验证书链和主机名。
- 不提供忽略证书校验或 `verify_none` 开关。
- Token 是服务端 JWT 身份凭据，不是证书；客户端不验签，服务端验签。
- `/dev/token` 仅用于开发环境；正式认证服务接入仍未完成。
- 断线重连使用 1/2/4/8/15 秒退避，服务端 resume 窗口内恢复原会话。
- 被踢、账号替换等 fatal close 不做自动重连。

## 7. 媒体架构

### 7.1 上行

```text
摄像头 MF / 屏幕 WGC-DXGI / 系统音 WASAPI
  -> I420 或 PCM/BGRA
  -> VideoPipeline / AudioPipeline
  -> WebRTC H264 / Opus
  -> SFU send PeerConnection
  -> Mediasoup transport
```

- 摄像头和屏幕共享共用发布 PeerConnection，但使用独立视频 m-line。
- 摄像头停止、屏幕共享停止和麦克风静音分别有独立发送状态。
- 屏幕共享支持屏幕/窗口源切换。
- 系统音使用 WASAPI loopback，可选择指定 render endpoint。

### 7.2 下行

```text
SFU recv PeerConnection / P2P PeerConnection
  -> libwebrtc H264 解码
  -> RemoteVideoSink → I420
  -> C ABI frame view
  -> Client Gateway latest-frame coalescing
  -> VideoGrid（UID + source 路由）
  -> muxPlayer Surface
```

关键约束：

- 每路远端视频按 `mid`、`uid`、`source` 注册。
- `sfu.offer` 重协商后主动刷新 receiver track，不能只依赖首次 `OnTrack`。
- UI 严格按 `UID + source` 路由，摄像头和屏幕流不能串流。
- 每流只保留最新帧；无帧 5 秒清屏并释放 Surface。
- RemoteTrack Sink 与 muxPlayer Surface 均独立，不共享可变帧队列。

### 7.3 SFU / P2P

- `sfu-send`：音频、摄像头、屏幕发布。
- `sfu-recv`：所有远端消费者订阅。
- `p2p`：1v1 超低延迟链路。
- 第三人加入时先建立 SFU、确认可用，再关闭 P2P。
- m-line 和 transceiver 由服务端 SDP 主导；客户端只做受控协商和轨道同步。

## 8. UI 与视频界面

UI 以 P5 `talk-desktop.exe` 为视觉和交互基线：

- 预入会页：连接、Token、设备、预览和入会。
- 房间页：视频区域、成员侧栏、设备页和状态页。
- 视频布局：宫格、聚焦、共享主画面、摄像头 PIP、底部成员胶片条。
- `VideoGrid`：场景布局、Tile 生命周期、Surface 增量绑定。
- `VideoSurfacePool`：最多 9 个 Surface，RAII Lease 和空闲复用。
- `FrameHealthMonitor`：Waiting/Healthy/Paused/Stale/Removed。
- muxPlayer：Raw I420、软件 OpenGL 渲染；WGL/D3D 零拷贝作为后续扩展。

UI 不在页面内解析信令 JSON，也不直接持有 WebRTC/采集对象。

## 9. 安全、凭证和诊断

- 服务端签发 HS256 JWT，客户端把 JWT 作为 Bearer Token。
- 手工 Token 可保存到 Windows Credential Manager。
- Credential Target 按 URL、UID、Room 隔离。
- JWT `exp` 用于过期判断和自动清理。
- QSettings 不保存明文 Token。
- 日志、诊断包和 trace 对 token、authorization、credential、password、
  SDP、ICE candidate 做脱敏。
- 当前 WSS 客户端已验证未受信任自签名证书会被拒绝；正式 CA 证书成功
  握手需要正式域名和证书环境后完成。

## 10. 构建

环境变量：

```powershell
$env:VCPKG_ROOT = 'D:\vcpkg'
$env:QTDIR = 'C:\Qt\5.15.2\msvc2019_64'
$env:WEBRTC_SDK_DIR = 'D:\google\webrtc_sdk_hevc'
```

Release：

```powershell
cmake --preset windows-x64
cmake --build --preset windows-x64-release --parallel
ctest --test-dir build/windows-x64 -C Release --output-on-failure
```

Debug：

```powershell
cmake --build --preset windows-x64-debug --parallel
ctest --test-dir build/windows-x64 -C Debug --output-on-failure
```

当前 CTest：`12/12`。

## 11. 运行和打包

运行：

```powershell
$env:PATH = "$env:QTDIR\bin;" + $env:PATH
.\build\windows-x64\bin\Release\rtc-talk.exe
```

便携包：

```powershell
pwsh -File .\scripts\package.ps1
```

打包内容包括：

- `rtc-talk.exe`
- `rtcwin_engine.dll`
- `rtcwin_log_adapter.dll`
- `log-cpp.dll`
- Qt runtime 和平台插件
- VC Runtime
- FFmpeg/fmt runtime
- `SHA256SUMS.txt`
- `VERSION.txt`

默认日志：

```text
%LOCALAPPDATA%\rtc-talk-win\logs\rtcwin.log
```

日志会轮转为 `rtcwin.1.log`、`rtcwin.2.log` 等。

## 12. 测试与验收

当前已有测试覆盖：

- Domain/Application 基础行为。
- SessionStateMachine 和 ReconnectController。
- VideoScene、FrameHealth、SurfacePool、9 路压力。
- Floor/Host 控制器。
- Signaling 协议。
- log-cpp C ABI。
- MediaSession SFU/P2P 冒烟。
- Windows Credential Manager 和 JWT 过期处理。
- WSS 未受信任证书拒绝路径。

生产发布前仍未完成：

- 正式域名/CA 证书 WSS 成功联调和完整错误验收。
- 正式认证服务、Token 刷新/轮换接口和 `/dev/token` 生产移除。
- 强制 TURN relay、断网超过 60 秒和网络切换。
- 30 分钟双人、2 小时多人、连续设备/视频切换长稳。
- 安装包、签名、升级、回滚、干净 Windows 10/11 验收。

## 13. 文档索引

- 需求和交付边界：[docs/requirements.md](docs/requirements.md)
- 构建和测试：[docs/build-and-test.md](docs/build-and-test.md)
- 架构总览：[docs/architecture/README.md](docs/architecture/README.md)
- 系统上下文：[docs/architecture/01-system-context.md](docs/architecture/01-system-context.md)
- 模块和目录：[docs/architecture/02-module-architecture.md](docs/architecture/02-module-architecture.md)
- 领域模型：[docs/architecture/03-domain-model.md](docs/architecture/03-domain-model.md)
- Engine 和会话：[docs/architecture/04-engine-and-session.md](docs/architecture/04-engine-and-session.md)
- 媒体架构：[docs/architecture/05-media-architecture.md](docs/architecture/05-media-architecture.md)
- 执行计划：[plan/gap-driven-development-plan.md](plan/gap-driven-development-plan.md)
- 长期计划：[plan/production-plan.md](plan/production-plan.md)
- 交接文档：[handover.md](handover.md)

## 14. 开发规则

- 只修改 `rtc-talk-win` 分支；历史参考工程只读。
- UI、应用、Engine、媒体和平台适配层不得越界直接依赖。
- 新功能必须有失败路径、资源释放和测试。
- 不允许把 Token、SDP、ICE credential 或原始设备端点写进长期日志。
- 每个阶段完成后更新计划和交接文档，再进入下一阶段。