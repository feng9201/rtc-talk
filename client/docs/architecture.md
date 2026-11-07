# 系统架构 v1

日期：2026-11-06。状态：实施基线；可调整项通过 ADR 记录，不偷偷改变协议。

## 1. 产品范围与约束

| 项目 | 首个可用版本 M1 | 生产版本 GA |
|---|---|---|
| 平台 | RK3576 / Buildroot / Qt 5.15 / Weston | 同一经认证的 BSP、固件和硬件清单 |
| 上行 | Opus 麦克风音频；默认静音 | 同左，设备异常时 fail-closed |
| 下行 | Opus 音频 + 1 路 H.264 视频 | 可配置多路；路数由实测资源预算批准 |
| 视频来源 | 远端 camera / screen，均可播放 | 主画面选择、布局、暂停/结束状态 |
| 协商 | SFU；不虚报 P2P 能力 | SFU 完整；P2P 作为独立扩展验收 |
| 房间功能 | 连接、入退房、成员、静音、音量 | 呼叫、PTT、主持管控、恢复、设备管理 |
| 安全 | WSS 架构与严格配置；开发 WS 显式隔离 | 证书验证、正式鉴权、权限、凭证轮换全部验收 |

M1 是完整纵向链路的最小可用应用，不是生产验收完成，也不是只显示假数据的 UI。
GA 是本计划的投产目标。不能把实验室 WS + `/dev/token` 当作生产登录方案。
不在本轮引入摄像头、RTSP、HTTP-FLV、AI、录像、Web 管理后台或新的信令协议。
H.265 SFU、服务端分层订阅/关键帧扩展没有实现证据，不能作为当前可用能力。

## 2. 现有系统与信任边界

```text
业务身份服务 ──短期 JWT──> RK 客户端
                              │ WSS / talk.v1.json / Bearer
                              ▼
                      talk-server（状态权威）
                              │ SDP / consumers / ICE
                              ▼
Windows 等远端 <──DTLS-SRTP──> mediasoup SFU <──DTLS-SRTP──> RK WebRTC
                                                            ├─ ADM/APM/Opus → 声卡
                                                            └─ H264 AU → MPP → NV12 DMA-BUF
                                                                                  │
                                                                    Wayland → Weston → 屏幕
```

服务端承担身份验证、房间权限、floor、Producer 控制、媒体路由。
客户端承担采播、硬解、渲染、交互、断线恢复和安全默认值，不本地猜测授权。
客户端没有对外监听 HTTP 端口；本地 API 是进程内调用，不另造 REST 后端。
身份服务和证书/TURN 部署属于上线依赖，现有开发服务器是否具备这些能力必须单独核查。

## 3. 技术选择与依赖方向

- C++17 业务内核：减少 SDK/交叉工具链兼容成本，禁止异常越过 C ABI。
- Qt5 Widgets：现有板端 Qt 可用；MVVM 风格 Presenter + Store，不在 QWidget 中操作协议。
- libwebrtc M146：唯一 RTP/RTCP、ICE、DTLS-SRTP、Opus、APM、同步调度实现。
- MPP：外部 `VideoDecoderFactory` 注入；不要求 SDK 内置 OpenH264。
- Wayland：参考 sbdc 当前实现，受控纳入最小必要源码及来源记录，不链接 sbdc 的 build 目录。
- log-cpp：固定版本的统一日志适配，所有模块只依赖自有日志接口。
- WebSocket/TLS：可替换传输端口；实现必须有证书链 + 主机名校验，不能禁验证。

实现约束：[ADR 001](adr/001-isolate-media-tls.md) 将媒体 SDK/BoringSSL 私有封装在独立动态库，
与信令系统 OpenSSL 隔离，禁止两套实现同名函数混合解析；构建自动检查依赖与导出。

依赖方向：`UI → Application → Domain`；`Infrastructure/Media → Ports + Domain`；
Composition Root 负责注入。Domain 不依赖 Qt、JSON、MPP、WebRTC、日志库或网络。
原音频原型仅作行为与测试参考，不把旧的集中式 Core 直接定义成生产架构。

## 4. 控制面与状态模型

`SessionController` 在单一控制执行器上串行修改状态：

```text
Idle → Connecting → Authenticating → Ready → Joining → Joined → Leaving → Ready
                 ↘ Failed                  Joined → Recovering → Joined
                                                               ↘ Ready / Failed
所有状态 → Stopping → Stopped
```

- 每次连接、房间/媒体重建分配 generation。过期 RPC、定时器、帧、track 回调必须丢弃。
- 命令、RPC、媒体操作分别有界，满载明确返回 Busy，不无限排队。
- 本地接受命令不等于服务端操作成功。每个已接受命令恰有一个终态结果。
- 双向 RPC 分离 client pending 表和 server request 表；SFU offer 串行处理并带截止时间。
- join、room snapshot、peer 及 media producer 是不同事实，不从“用户开启视频”推断一定有帧。
- 入房不覆盖既有房间策略，默认不附带 policy；有意创建时才提交指定 policy。
- 用户明确退出、401/403、replaced/kicked、恢复窗口过期停止恢复；不无限自动重新入房。
- 网络恢复按 TTL resume，成功后以快照重建；没有 resumed 的新会话回 Ready，要求用户重新入房。
- 控制断线立即关闭本地麦克风 gate，即使媒体连接暂时仍通也不得泄漏音频。
- shutdown 顺序：关麦 → 停新命令 → 取消 RPC/定时器 → 解绑 sinks → 关闭 PC →
  回收媒体任务 → 释放未提交帧/Wayland 资源 → 停线程 → flush 日志。

## 5. 音频链路和权限

采集：ADM → APM（AEC/NS/AGC）→ 最终安全 gate → Opus → send PC（audio/sendonly）。
播放：recv PC → Opus/NetEq → WebRTC 音频混音 → ADM。不得经 sbdc AAC/miniaudio 再播一份。

gate 必须位于最终编码前的可控路径并实测；如果 SDK 的处理顺序不同，先核实再选择接点。
发送权限 = 用户意图 ∧ 服务器许可 ∧ 会话有效 ∧ 设备健康 ∧ floor/主持策略许可。
默认、断网、设备失败、撤销、抢麦释放/失焦均立即静音；开麦只在成功授权后执行。
PTT 松开先本地关麦再发 release；断网时不等待服务端释放才关麦。
ADM 保持 WebRTC 音画同步使用的播放时钟；设备使用稳定标识，不把枚举索引写作永久配置。
启动不擅自修改 ALSA mixer，也不抢占既有业务声卡；真实采播必须先确认设备映射。

## 6. 视频链路（最重要的集成边界）

```text
recv PC → SRTP/RTP 重组 → WebRTC jitter/NACK/PLI → EncodedImage（完整访问单元）
 → MppVideoDecoder → DecodedImageCallback(NativeVideoFrameBuffer)
 → WebRTC 视频调度 / 与音频同步 → VideoSink::OnFrame
 → 每流 latest-frame mailbox（1 个未提交帧）
 → Wayland 显示执行器 → NV12 wl_buffer → compositor release → 归还 MPP 帧
```

1. MPP 接收的是解包后的 H.264 AU，不是 UDP/RTP 包。编码识别来自 SDP format，绝不写死 PT=102。
2. `rtc_use_h264=false` 的 SDK 不能提供内置 H.264 解码器；外部工厂明确声明 H264 CB
   `profile-level-id=42e01f, packetization-mode=1`（与服务器当前能力一致），经实际协商验证。
3. send PC 不创建 video sender/encoder/capture；recv PC 可以回答 video/recvonly。
   不通过删除远端 video m-line、乱改 SDP 或空 codec 工厂来规避接收。
4. MPP 解码工作在可停止的专属队列；提交失败、超时、损坏 AU 不阻塞信令/UI。
   输入队列同时限制字节、条数、等待时间；过载进入等待关键帧，不任意丢掉参考帧后继续解。
5. 帧包装保存 MppFrame、池 generation、fd/planes/stride/offset/format、RTP timestamp、
   render timestamp、色彩/旋转元数据。fd 是借用句柄，真正所有权在 lease。
6. 帧池/解码器上下文由共享租约延寿。分辨率变化创建新池 generation；旧池在旧帧全部归还后回收。
   不能因 decoder Release/reset/info_change 提前释放 compositor 还持有的 DMA-BUF。
7. 限定输出 NV12；核对 MPP 实际 format、stride、plane offset、buffer size、modifier 和 crop。
   不将 AFBC、10bit 或错误布局冒充线性 NV12。格式不支持明确失败/拒绝，不显示花屏。
8. 未提交 pending 帧可被替换释放；**已提交帧不能因 50ms 超时强制归还**。
   必须等 wl_buffer.release 或确认该显示连接已被销毁、不再能访问这些缓冲后清理。
9. 不叠加 sbdc NVR 的 150ms jitter buffer、RTSP DTS 时钟或每流长播放队列。
   保留 WebRTC 调度，显示侧只做有界 vsync 提交，不另造第二套 A/V 时钟。
10. 解码 reset 后请求关键帧，限频且有恢复上限；没有可用关键帧机制不得假定 reset 一定能恢复。
    首先验证 M146 decoder 返回值触发关键帧请求路径；不调用服务端预留未实现的 keyframe API。
11. 收到明确 producer paused/closed 时显示“已暂停/已结束”，与“收流但解码失败”分开。
    低帧率屏幕共享合法；仅无新帧不直接判定硬件卡死，应结合输入、paused 和输出进度。

M1 允许 1 路视频，选择优先级为用户固定 > screen > camera，再按稳定到达顺序。
可解码资源不是所有房间视频的自动解码许可。当前服务端缺少选择性 consumer 控制的证据，
现有 `server/src/media/room-media.ts::negotiateOnce` 对所有未关闭 Consumer 调用 resume，
`signaling.ts` 没有将 max_recv 应用于订阅过滤。M1 先验证超出预算的 video m-line 可拒绝而不破坏音频；
若服务端不能正确处理则列为服务端依赖，
不得默默解码所有路、仅隐藏画面。生产多路上限需板测和 admission-control 共同确定。

## 7. UI 与 Wayland 合成

采用 sbdc 已验证的“透明 Qt 视频区域 + 独立 Wayland 视频窗口/子 surface”机制，
不是在 Qt paintEvent 中把每帧转 QImage，也不是直接混用 Qt 内部 Wayland 连接指针。
具体宿主窗口定位能力依赖 Rockchip Weston 扩展，必须检测；不能假定标准 xdg-shell 能任意置底/定位。
窗口配置、选择屏幕、缩放、移动/resize 由 UI 发结构化布局命令，协议写统一串行化。
Qt 线程不等待 configure、registry roundtrip 或 decoder drain。

当前 sbdc 源码还包括热插拔静默、ScreenName 屏幕锚点、异步创建和共享位置校验，
比 README 简略图更完整，应按当前实现提取，不回退到历史 sleep 限帧版本。
整台 Weston 死亡可能导致 Qt Wayland 插件退出，不能承诺纯进程内恢复；由部署 supervisor
在有界重试/退避后重启应用，应用重启必须静音，不能自动恢复开麦意图。
不在共享测试板执行重启 Weston 或高风险热插拔故障注入。

## 8. 线程、队列与内存预算

| 执行域 | 职责 | 不得做 |
|---|---|---|
| Qt UI | Store/Presenter/Widgets/布局 | 网络同步等待、解码、跨线程 QWidget 访问 |
| 控制 executor | 状态机、RPC、权限、恢复 | 调 libwebrtc BlockingCall 等待媒体结果 |
| WebRTC network/worker/signaling | SDK 内部网络及媒体 | 访问 Qt 对象、磁盘日志阻塞 |
| MPP decode queue | MPP 提交/取帧/池管理 | 无上限重试、忙自旋、非 joinable 线程 |
| Wayland display | poll/dispatch、commit/release | 持锁 sleep、调用 UI 重建/自 join |
| 日志 worker | 有界异步写盘与轮转 | 接收原始凭证或每帧大数据 |

队列预算基线：命令 128、RPC 64、消息 256KiB、媒体任务 32、单视频输入 8 AU/4MiB，
未提交显示帧 1。数值是可测配置上限，需按流量/分辨率校准，不是吞吐承诺。
队列满有显式结果与计数；最高优先级本地 mute gate 不依赖普通队列成功入队。
在途显示帧额外计数并限制提交数，满则停止新提交，不丢失已提交租约。
输出池内存预算基于实际 MPP buffer size × buffer count（含 DPB 和在途），不能只按 w×h×1.5 估算。

## 9. 安全、可观测性、部署

- 生产 WSS 校验 CA/hostname/期限，无跳过校验选项；开发 WS 仅显式 development profile。
- JWT 内存持有，禁止放 URL query、命令行、仓库、日志、QSettings；正式 token provider
  注入短期凭证，过期提示认证，不把 dev-token 接口编进生产登录流程。
- TURN 临时凭证按 TTL 刷新；生产必须验证 relay-only 场景，并具有明确 NAT 失败提示。
- 解析限制消息大小/结构/字符串长度；未知方法 501；回调不传播异常；所有整数/大小校验溢出。
- 日志只写允许字段：operation/generation/stream/错误码/耗时。不得打印完整 SDP、JWT、ICE 密钥、密码。
- 日志轮转总量有界，磁盘满不影响关麦；实时音频回调仅原子计数，不写盘。
- 指标：连接、RTT、loss、jitter、concealment、码率、AU/解码/提交/丢帧数、解码耗时、
  MPP reset、pending/inflight leases、RSS/FD/线程数。避免 uid 等无界标签。
- 包中包含版本、Git revision、依赖来源/hash/license、配置 schema、符号包和诊断工具。
- 私有包目录与相对 RPATH；不开系统库覆盖、不使用 `--allow-shlib-undefined` 掩盖缺符号。
- 开发 `/tmp` 部署；GA 根据设备实际 init 系统提供 supervisor 适配，不能预设有 systemd。
- 更新使用版本目录、校验、原子切换和可回滚配置迁移；不开未经授权自动远程更新端口。

## 10. 已核实依据与待验证事实

只读参考工程：`sbdc/README.md`、`src/codec/rkmpp_codec/`、
`src/demux/webrtc_demux/`、`render/wayland_render.md`、`render/src/sbdc_wayland_render.cpp`、
`wayland/include/wayland.h`。muxDecoder 已废弃，不纳入方案。
本机 SDK 的 `api/video_codecs/video_decoder{,_factory}.h` 支持外部工厂；
`api/video/video_frame_buffer.h` 支持 kNative 帧。接口存在不等于板端集成通过。
服务端以根目录 [现有协议](../../plan/talk-protocol.md) 和实际 handlers 为事实来源。
仍待板测：外部 H264 协商、NV12 modifier/stride、关键帧恢复、Qt 合成层级、真实声卡/AEC、
音画偏差、多路与热插拔资源上限，以及线上 WSS/身份/TURN 运维配置。
