# rtc-talk-win 缺口驱动开发计划

> 基线：`rtc-talk-win@f2cc1a5`，M1 已提交并推送
> 日期：2026-10-30  
> 目标：在保持 P5 UI 基线和生产模块边界的前提下，逐项补齐旧客户端能力。  
> 原则：旧工程只作为行为样本，不直接复制实现；每阶段必须有明确接口、状态模型、测试和验收门禁。

## 0. 计划归属与主计划映射

本文件不是替代 `production-plan.md` 的新计划，而是长期主计划 S0-S8 的
“缺口交付执行分册”。计划关系固定为：

- `plan/production-plan.md`：项目长期主计划和原始 S0-S8/P0-P5 范围。
- `plan/gap-driven-development-plan.md`：当前唯一执行分册，负责 M1-M7 顺序、
  接口、验收和排期。
- `handover.md`：只记录现状、提交、证据和下一步，不重复展开另一套计划。

| 执行阶段 | 对应主计划阶段 | 当前状态 |
|---|---|---|
| M1 视频场景和双路呈现 | S4.2 中的 UI/双流呈现部分 | 已完成，待用户阶段确认 |
| M2 多路 Surface 和帧健康 | S4.2 剩余部分 | 已完成，待用户阶段确认 |
| M3 Floor、PTT 和主持人 | S5 | 下一阶段 |
| M4 房间设备与屏幕源 | S6 | 未开始 |
| M5 重连、resume 和 ICE 恢复 | S7 | 未开始 |
| M6 诊断和可观测性 | S6/S7 | 未开始 |
| M7 安全和发布 | S7/S8 | 未开始 |

### 0.1 阶段估算

| 阶段 | 估算 |
|---|---:|
| M2 | 2–4 个工作日 |
| M3 | 3–5 个工作日，依赖服务端 floor/host 协议稳定 |
| M4 | 3–5 个工作日 |
| M5 | 2–4 个工作日 |
| M6 | 2–4 个工作日 |
| M7 | 2–4 个工作日，不含签名证书和发布环境等待 |

总工程量粗估：14–26 个工作日，不含用户验收等待、长时间稳定性测试和
第三方发布审批。

## 1. 当前结论

已具备：

- 摄像头、屏幕共享、P2P 摄像头、P2P 屏幕共享的媒体传输。
- `audio/camera/screen` 三 m-line。
- SFU/P2P 媒体模式切换基础链路。
- AudioPipeline、VideoPipeline、MediaPeer、RemoteTrackRegistry 等媒体模块拆分。

当前主要缺口：

- 多路 Surface、无帧回收、9 路性能未完成。
- Floor/PTT、主持人操作和实时音量未接入 Engine。
- 房间设置、设备面板、状态面板仍为占位。
- 设备热插拔、多屏/窗口共享、系统音端点选择未完成。
- resume、ICE refresh/restart、WSS 和发布工程未完成。

### 1.1 M1 实施状态

M1 已完成代码实现、自动门禁和阶段证据，等待用户按流程确认：

- `VideoSceneBuilder` 已迁移到 application 层，输出与 QWidget 无关的场景模型。
- `VideoLayoutEngine` 已独立到 UI 层，只计算 Grid/Focus/Shared 布局策略。
- 视频 Tile 键从 `uid` 改为 `uid + source`，camera/screen 可同时存在。
- Shared 场景使用 screen 主 Tile，并保留同一成员的 camera PIP Surface。
- 本地帧缓存从单槽改为 `uid + source` 多流缓存，摄像头与屏幕不会互相覆盖。
- `VideoGrid` 修复了“缓存插入后重新查键失败”的帧路由缺陷；该问题会导致远端
  帧已到达 UI 但 Tile 始终显示占位。
- `MuxVideoSurface` 按 16:9 等比裁切铺满，避免 muxPlayer 拉伸，并向 PIP
  提供圆角裁切和独立“摄像头”标签。
- Shared 布局已按 P5 调整为“全宽共享主画面 + 底部成员胶片条”，PIP 固定在
  主画面右下角。

M1 证据：

```text
build/windows-x64/m1-camera-preview-cover.png
build/windows-x64/m1-route-fixed-20261030-131603/b.png
build/windows-x64/m1-dual-final-20261030-131626/b.png
build/windows-x64/m1-p2p-p5-20261030-132435/b.png
build/windows-x64/m1-stop-p5-20261030-132505/b-after-stop.png
build/windows-x64/m1-stop-camera-20261030-132838/b-after-camera-stop.png
```

M2 已完成：

- `VideoSurfacePool` 和 RAII Lease，活动 Surface 上限为 9。
- `FrameHealthMonitor` 统一管理 Waiting/Healthy/Paused/Stale/Removed。
- 5 秒无帧清屏并释放 Surface，新帧恢复后重新租赁。
- producer paused、peerLeft、media.track state 已进入快照。
- 9 路 720p 注入 29 ms；100 次显示/布局切换 Surface 数不增长。
- Release/Debug CTest 9/9。

M1、M2 当前等待用户确认。M3 Floor/PTT/主持人尚未开始。

## 2. 目标架构

```text
Engine
  SessionController
  FloorController
  HostController
  DeviceCoordinator
  MediaOrchestrator

Application
  SessionViewModel
  VideoSceneBuilder
  FrameHealthMonitor
  DiagnosticsViewModel

UI
  VideoScene
  VideoLayoutEngine
  VideoTileComposer
  VideoSurfacePool
  RoomPage / SettingsDialog / DevicePanel
```

### 2.1 视频呈现

视频域不再以 `uid` 作为唯一键，统一使用：

```cpp
struct VideoStreamKey {
  std::string uid;
  VideoSource source;  // camera | screen
};
```

职责：

- `VideoSceneBuilder`：将 SessionSnapshot、VideoStreamState 组装成场景。
- `VideoLayoutEngine`：只计算布局，不操作 QWidget。
- `VideoTileComposer`：渲染单流或“共享主画面 + 摄像头 PIP”复合 Tile。
- `VideoSurfacePool`：限制活动 Surface 数量，提供复用和回收。
- `FrameHealthMonitor`：5 秒无帧、producer paused、peer left 统一清理。

### 2.2 Floor / Host

- `FloorController` 独占 floor.request/release/state/revoked。
- 本地麦克风门由 floor 状态机驱动，不允许 UI 自造权限。
- `HostController` 独占 host.grant/muteAll/kick/setPolicy。
- UI 只发命令和展示权威快照。

### 2.3 Device

- `DeviceCoordinator` 统一管理麦克风、播放、摄像头、屏幕源。
- 设备热插拔、占用、拔出、默认设备变化进入状态机。
- 媒体层只接受已解析的 DeviceSelection。

### 2.4 Resilience

- `ReconnectController` 管理 resume、退避、过期和失败终止。
- `IceRecoveryController` 管理 refreshIce、restartIce 和 candidate 缓存。

## 3. 分阶段交付

### M1 视频场景和双路呈现

状态：已完成并推送；等待用户阶段确认。

范围：

- `VideoStreamKey(uid, source)`。
- `VideoSceneBuilder` / `VideoLayoutEngine`。
- camera/screen 独立 Tile。
- 共享主画面 + 摄像头 PIP。
- 本地画面与远端画面使用同一场景模型。
- `MuxVideoSurface` 等比裁切、圆角和 PIP 标签。

验收：

- 同一成员同时开摄像头和屏幕，主画面为屏幕，右下角显示摄像头 PIP。
- 停止屏幕后自动回到摄像头 Tile。
- 停止摄像头不影响屏幕画面。
- 双人 SFU、双人 P2P 各截图验收一次。

### M2 多路 Surface 和帧健康

前置：M1 通过用户验收后再开始，避免在场景模型未确认时提前固化 Surface 池接口。

范围：

- `VideoSurfacePool`。
- `FrameHealthMonitor`。
- producer paused、peer left、5 秒无帧清理。
- 隐藏 Tile 降帧或停止渲染。

验收：

- 同一客户端 9 路视频无串流。
- 断流 5 秒内清空对应 Tile。
- Surface 反复创建/释放无持续增长。
- 9 路 720p 短时性能报告。

已验证：

- 9 路 720p 帧注入 29 ms，9 个 stream key 独立。
- 100 次显示/布局切换后 Surface 数不增长。
- 暂停发送端后，接收端 5 秒清屏并释放 Surface。
- producer paused 后共享画面立即清除并回退 camera Grid。
- Release/Debug CTest 9/9。

证据：

```text
build/windows-x64/m2-pool-smoke-20261030-164503/b.png
build/windows-x64/m2-stale-smoke-20261030-165010/b-after-stale.png
build/windows-x64/m2-paused-smoke-20261030-165144/b-after-pause.png
```

### M3 Floor、PTT 和主持人

范围：

- `FloorController`。
- `HostController`。
- floor.request/release/state/revoked。
- host.grant/muteAll/kick/setPolicy。
- `room.subscribeLevels` 和实时成员音量。

验收：

- free / ptt / moderated 三种模式切换。
- PTT 按下申请、释放、超时、排队和收回。
- 主持人授权、全员静音、踢人和策略修改。
- 音量条有实时服务端数据。

### M4 房间设备与屏幕源

范围：

- 房间设置弹窗。
- 设备面板、状态面板。
- 会议中麦克风/播放/摄像头切换。
- 设备热插拔、拔出、占用和默认设备恢复。
- 多屏、窗口共享选择。
- 非默认系统音 loopback 端点。

验收：

- 会议中切换设备不断房。
- 拔出设备后自动回退并提示。
- 可选择指定屏幕、窗口和系统音端点。

### M5 重连、resume 和 ICE 恢复

范围：

- `ReconnectController`。
- session.resume、退避、恢复过期。
- session.refreshIce、sfu.restartIce。
- P2P 失败/超时回退。
- kicked/replaced/fatal 关闭处理。

验收：

- 断网 10 秒内恢复原房间和媒体。
- TURN 凭证刷新成功。
- ICE 失败后 SFU/P2P 按策略恢复。
- 恢复过期回到预入会并给出明确错误。

### M6 诊断和可观测性

范围：

- RTCStats 完整聚合：RTT、jitter、loss、bitrate、candidate pair。
- 房间状态面板实时数据。
- 诊断包导出。
- 日志关联 session/generation/track。

验收：

- UI 能看到连接、码率、丢包、RTT 和远端流状态。
- 一键导出脱敏诊断包。
- 日志可关联一次会话的完整时间线。

### M7 安全和发布

范围：

- WSS/TLS。
- Token Credential Manager。
- 安装包、签名、升级和回滚。
- 版本迁移和依赖清单。

验收：

- WSS 证书校验通过。
- Token 不明文落盘。
- 安装、升级、回滚和卸载通过发布门禁。

## 4. 跨阶段门禁

每个阶段必须通过：

1. Release 构建。
2. Debug 构建。
3. 单元/组件 CTest。
4. 两客户端真实服务短链路。
5. 日志无未脱敏凭证。
6. 更新 `handover.md` 和 `production-plan.md`。

## 5. 明确不在本计划

- `call.invite` 呼叫邀请弹窗。
- H.265 P2P。
- Simulcast 和多层订阅。
- 大规模长期稳定性测试；由用户单独安排。
