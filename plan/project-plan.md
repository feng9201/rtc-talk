# WebRTC 对讲服务 项目计划

> 配套文档：[信令协议规范](./talk-protocol.md)

## 1. 需求基线

| 项 | 一期 | 后续 |
|---|---|---|
| 媒体 | 纯语音 | 音视频（方案一期设计完成，预留协议） |
| 对讲形态 | 全双工自由通话为默认；PTT、主持人管控作为房间策略同时支持 | — |
| 媒体模式 | SFU（多人）+ P2P（1v1），自动选择与切换 | 多节点级联 |
| 规模 | 单房间 ≤ 20 人 | 更大房间/多节点 |
| 服务端 | Linux | — |
| 客户端 | Windows、macOS、Linux arm64（RK3576） | RK3568；浏览器；低端 RK（RV11xx）用 metaRTC/libdatachannel |

## 2. 技术选型结论

| 层 | 选型 | 说明 |
|---|---|---|
| 信令/业务服务 | **Node.js + TypeScript（talk-server）** | 与 mediasoup 控制 API 同进程，开发效率高；WS 用 `ws`，JSON 协议 |
| SFU | **mediasoup v3** | C++ worker；使用 `WebRtcServer` 单端口；ActiveSpeaker/AudioLevel Observer 支撑发言状态 |
| SDP 适配 | 服务端复用 mediasoup-client 的 `ortc` + `RemoteSdp` 逻辑 + `sdp-transform` | 客户端只说标准 SDP，不依赖 libmediasoupclient |
| STUN/TURN | **coturn** | `use-auth-secret` 临时凭证；UDP 3478 / TCP 3478 / TLS 5349 |
| 客户端媒体引擎 | **Google libwebrtc M146**（branch-heads/7680） | 三端同一版本；APM(AEC3/NS/AGC2)、NetEQ、TWCC |
| 客户端 WebSocket | IXWebSocket 或 Boost.Beast | libwebrtc 不含 WS 客户端 |
| 客户端 JSON | nlohmann/json | — |
| 存储/状态 | Redis（在线状态、会话恢复、二期多节点） | 一期单节点可内存实现，接口按 Redis 设计 |
| 监控 | Prometheus + Grafana；客户端 `sfu.stats` 上报 | — |
| 旁路（二期） | ZLMediaKit | 录制、转 RTSP/RTMP、对接 GB28181 设备 |

## 3. 系统架构

```
                         ┌───────────────────────────────────────────────┐
  业务后台 ──签发 JWT──►  │                 talk-server (Node.js)          │
                         │  WS 网关 · 鉴权 · 会话 · 房间 · 呼叫 · 发言权   │
                         │  媒体模式决策 · SDP 适配 · TURN 凭证 · 指标     │
                         └──────┬─────────────────────────────┬──────────┘
                                │ mediasoup API (进程内)       │ HMAC secret
                     ┌──────────▼──────────┐           ┌──────▼──────┐
                     │ mediasoup workers   │           │   coturn    │
                     │ (每核一个, WebRtcServer│          └──────┬──────┘
                     │  单端口 UDP/TCP)      │                 │
                     └──────────┬──────────┘                  │
            SFU: SRTP           │              P2P: 直连 / TURN 中继
  ┌─────────────────────────────┴──────────────────────────────┴───────────┐
  │                     talk-core（C++ 共享库，导出 C API）                    │
  │  SignalingClient · RoomSession · ModeController(P2P/SFU)                 │
  │  MediaEngine(libwebrtc: ADM/APM/Opus) · P2PTransport · SfuSend/RecvPC    │
  ├───────────────┬────────────────┬────────────────────────────────────────┤
  │ Windows (WASAPI)│ macOS (CoreAudio)│ Linux arm64 RK3568/3576 (ALSA/Pulse) │
  └───────────────┴────────────────┴────────────────────────────────────────┘
                     UI：Qt，通过 C API 调用 talk-core
```

### 3.1 客户端关键设计

- **talk-core 编译为单个共享库**（`.dll/.dylib/.so`），libwebrtc 静态链接在内部，只导出 C API。规避三端 C++ ABI / CRT / libc++ 冲突（现有 SDK 为 `/MT`、无异常，直接暴露给业务工程风险高）。
- 三类连接统一抽象为 `PeerTransport`：`P2PTransport`、`SfuSendTransport`、`SfuRecvTransport`。模式切换只是替换 transport，`RoomSession` 状态不动。
- 音频设备与 3A：用 libwebrtc 内置 ADM；RK 板若 codec 芯片支持硬件回采，可切 `aec:"hw"`；否则 AEC3 软件处理（A55 单核占用预计 < 10%，P0 实测）。
- 视频预留：`MediaEngine` 暴露 `VideoEncoderFactory/DecoderFactory` 注入点，二期接入 RK MPP、Media Foundation、VideoToolbox。

### 3.2 服务端关键设计

- 一个房间对应一个 Router，整个房间放在同一个 worker 上（20 人房间 380 个 Consumer，单 worker 足够）；新房间按 worker 负载分配。`pipeToRouter` 跨 worker 留到二期大房间再做。
- 全量订阅（详见协议 6.4）：入房即订阅所有人，开关麦和抢麦只暂停/恢复 Producer，不重协商。
- recv 协商按成员串行排队 + 100ms 合并窗口，应对多人同时入房。
- 媒体模式决策集中在 `ModePolicy`：2 人且双方 `caps.p2p=true` → P2P；其余 → SFU；P2P 失败/人数增加 → 迁移到 SFU。

### 3.3 部署（单节点一期）

| 组件 | 端口 | 备注 |
|---|---|---|
| talk-server WSS | TCP 443（或 8443） | 前置 Nginx 终结 TLS 可选 |
| mediasoup WebRtcServer | UDP+TCP 44444（每 worker 一个端口：44444~4444N） | 配置 `announcedAddress` 为公网 IP |
| coturn | UDP/TCP 3478，TLS 5349，中继 UDP 49152-65535 | 与 talk-server 共享 secret |
| Redis | 6379（内网） | — |

建议规格：8C16G、公网带宽 ≥ 100Mbps，按典型场景（20 人、同时 2~3 人说话，房间出流量约 2.4Mbps）估算可支撑 30 个以上满员房间，瓶颈通常在带宽而非 CPU，P4 压测校准。

## 4. 里程碑与计划

假设团队：服务端 2 人、客户端 3 人（Windows/macOS 1、RK/Linux 1、核心库 1）、测试 1 人。

| 阶段 | 周期 | 目标 | 交付物 | 验收标准 |
|---|---|---|---|---|
| **P0 技术预研** | 第 1-2 周 | 消除最大不确定性 | 1) libwebrtc M146 的 macOS(arm64/x64)、Linux arm64 构建脚本与产物；2) RK3568/3576 上 libwebrtc 音频回环 demo（ADM+AEC3）；3) SDP 适配 spike：原生 libwebrtc PC ↔ mediasoup 通音频；4) coturn 部署 | RK 板双向通话回声可接受；SDP 适配可行（否则改用 libmediasoupclient 并评估移植 M146 的成本）；输出选型决策记录 |
| **P1 SFU 语音 MVP** | 第 3-6 周 | 多人全双工通话打通 | 服务端：WS 网关、JWT、session/room/media/sfu 方法、mediasoup 集成、SDP 适配、订阅策略；客户端：talk-core 骨架 + Windows 端 demo | Windows 10 人同房间稳定通话 1 小时；开关麦、进出房间正确；端到端延迟 < 300ms（局域网） |
| **P2 P2P 与模式切换** | 第 7-9 周 | 1v1 呼叫 P2P、失败回退、加人转 SFU | call.* / p2p.* 全流程、ModePolicy、make-before-break 切换、TURN 凭证 | 同网/跨 NAT/对称 NAT 三种场景接通率 100%（含 TURN）；P2P→SFU 切换无明显断音（< 500ms） |
| **P3 多端移植** | 第 8-11 周（与 P2 并行） | macOS、RK3568、RK3576 可用 | 三端 talk-core 产物、示例程序、RK 音频设备适配（ALSA 设备选择、采样率、回采） | 三端与 Windows 互通；RK 板外放场景无明显回声；CPU 占用达标 |
| **P4 对讲形态与规模** | 第 12-13 周 | PTT、主持人、20 人房间 | floor.* / host.*、发言人检测、音量通知、多房间 worker 负载分配、压测工具 | 20 人房间全员开麦稳定 2 小时；多房间并发达到容量目标；PTT 按键出声 < 300ms；服务端资源曲线与容量模型 |
| **P5 稳定性与上线** | 第 14-16 周 | 可上线 | 断线恢复、ICE restart、网络切换、弱网测试（tc netem 丢包 10%/30%、抖动 100ms）、监控告警、运维文档、部署脚本 | 弱网 20% 丢包可懂度可接受；WS/网络切换后 5s 内恢复；监控覆盖核心指标 |
| **二期：视频** | +6~8 周 | 音视频对讲 | SDK 以 `rtc_use_h265=true`（按需 `rtc_use_h264`）重编；RK MPP、MF、VideoToolbox 硬编解码器工厂；Simulcast；选层 | 9 宫格视频 720p；RK 板硬编 1080p30 |
| **二期：浏览器** | +3~4 周 | Web 接入 | Web SDK（原生 RTCPeerConnection + 同一协议） | Chrome/Edge/Firefox/Safari 互通 |
| **二期：扩展** | 按需 | 录制、级联、低端板 | ZLMediaKit 旁路录制；多节点 + Redis；metaRTC/libdatachannel 适配 RV11xx | — |

一期总工期约 **16 周**（P2/P3 并行，按人工团队估算）。

## 5. 一期工作分解（WBS）

### 服务端（talk-server）

1. 工程脚手架：TS、配置、日志、Docker 镜像
2. WS 网关：消息封装、请求/应答匹配、心跳、限流
3. 鉴权：JWT 校验、TURN 临时凭证
4. 会话管理：session、断线恢复（resume）
5. 房间管理：成员、策略、通知广播
6. 媒体控制：worker 池、Router 分配、WebRtcServer、transport 生命周期
7. SDP 适配层：offer → produce 参数；consumer → recv offer；m-line 复用
8. 订阅管理：成员进出时增删 consumer、合并协商、m-line 复用
9. 呼叫与 P2P 转发、ModePolicy、迁移编排
10. 发言权：PTT 队列、超时收回、主持人操作
11. 指标与管理 API：房间/成员列表、强制解散、Prometheus
12. 压测工具：基于 mediasoup-client（Node 版）或 libwebrtc 无头客户端批量模拟

### 客户端（talk-core）

1. 构建系统：CMake 统一三端；libwebrtc 产物管理（Windows 已有，macOS/arm64 新建）
2. SignalingClient：WS、重连、请求管理
3. MediaEngine：Factory、ADM 设备枚举/切换、APM 配置、Opus 参数
4. PeerTransport 三实现 + SDP 处理
5. RoomSession 状态机 + ModeController
6. C API 设计与绑定示例（建议先出 Qt demo）
7. 平台适配：Windows（WASAPI、设备热插拔）、macOS（权限、签名公证）、RK（ALSA、交叉编译 sysroot、开机自启/守护）
8. 质量上报：getStats → `sfu.stats`

### 测试

1. 协议一致性用例（每个 method 正反例）
2. 网络矩阵：局域网、公网、NAT 类型、4G/5G 热点
3. 弱网：tc netem 丢包/抖动/限速
4. 音质：回声、啸叫、双讲、可懂度（主观 MOS 打分）
5. 容量：20 人房间全员开麦、多房间并发、长稳 24h

## 6. 主要风险与对策

| 风险 | 影响 | 对策 |
|---|---|---|
| libwebrtc 交叉编译到 RK（sysroot、glibc 版本） | 阻塞 P3 | P0 优先做；sysroot 用与板卡系统一致的 Debian 版本；产物以 `.so`+C API 交付，隐藏内部 libc++ |
| RK 板外放回声（硬件回采缺失、时钟漂移） | 音质不可用 | P0 实测 AEC3；必要时用支持回采的 codec 或外置 DSP；提供耳机/半双工降级 |
| 服务端 SDP 适配复杂度 | P1 延期 | P0 spike 验证；备选 libmediasoupclient（需评估移植 M146） |
| 多人同时入房的重协商 | 入房慢、信令拥塞 | 合并窗口、串行协商、m-line 复用 |
| 后续扩大房间人数 | 全量订阅不再适用 | 协议已预留 `max_speakers`；超过约 50 人时改为"只订阅开麦成员"并引入 pipeToRouter |
| 企业网屏蔽 UDP | 无法通话 | mediasoup WebRtcServer 开 TCP；coturn 开 TLS 443 |
| H.264/H.265 专利与授权（二期） | 法务风险 | 优先硬件编解码器（由芯片/系统厂商授权）；软件编解码器按需开启并评估授权 |

## 7. 待确认事项

### 7.1 已确认（2026-10-19）

| # | 事项 | 结论 |
|---|---|---|
| 1 | 鉴权与房间创建 | 房间由客户端即时创建；一期使用开发 token（`/dev/token`），业务后台 JWT 签发方后续再接 |
| 2 | 客户端 UI 技术栈 | Qt |
| 3 | 通话录制、录音存档 | 一期不做，ZLMediaKit 旁路放到后续 |
| 4 | 正式部署环境 | 暂不考虑，一期只面向阿里云测试机 |
| 5 | 信令传输 | 测试期走 `ws://IP`，不上 TLS；coturn 暂不开 TLS 443 |
| 6 | RK 平台 | 一期只支持 RK3576（Buildroot，glibc 2.38，见 RK3576 编译计划）；RK3568 暂缓 |

### 7.2 仍待确认

1. RK3576 板的音频 codec 型号、是否带硬件回采（决定 `aec:"hw"` 是否可用）。
