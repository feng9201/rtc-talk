# rtc-talk-win 详细开发计划（muxPlayer 优先）

> 状态：S1、S2、S3.1 已完成；S3.2 音频 P2P/SFU 主链路已提交；S4.1 摄像头 SFU 发布与远端 muxPlayer 渲染已通过双端短链路验证；S4.2 屏幕共享与 P2P 视频已通过双端截图验收；B1-B5 已完成。多路视频池、9 路压力和 5 秒无帧回收仍待完成。
> 日期：2026-10-29
> 当前代码基线：`rtc-talk-win@b3af0e8`
> 第一验收目标：完成 muxPlayer 接入，并用本机摄像头完成真实预览联调。
> 约束：WebRTC 音频仍由 WebRTC AudioDeviceModule 处理；muxPlayer 只作为视频渲染基础设施。

## 1. 当前基线

S1 已完成：

- 独立工程 `rtc-talk-win`，模块保持 domain/application/client_api/engine/client_gateway/ui 分层。
- `rtcwin_engine.dll` 提供版本化 C ABI、异步命令队列和本地 I420 回调。
- Media Foundation 摄像头枚举、NV12 采集和 I420 转换已接入 Engine。
- Client Gateway 消费本地帧，并采用“只保留最新帧”的 UI 投递背压。
- muxPlayer 完整源码树已迁入 `third_party/muxplayer`，S1 启用软件 OpenGL 渲染子集。
- muxPlayer 用户依赖 mtAudio、mtAudioPlayer、tmcore、flvparser 已完整迁入 `third_party`。
- FFmpeg 依赖由私有 vcpkg registry 固定版本获取；Qt/VS 仅作为开发环境。
- P5 MEDIA 预入会 UI、主题资源、胶囊控件、窗口控件和摄像头重新扫描已移植。
- 已修复静态 UI 库 QRC 资源未初始化导致 `theme.qss` 不生效的问题。
- S2 已完成 WebSocket、开发 token、session.hello、room.join/leave、心跳和成员快照。
- P5 房间页圆形控制栏及设置按钮已对齐。
- Release、Debug 构建通过，CTest 2/2 通过，真实摄像头和服务端联调已验证。

S3 当前进度：
- libwebrtc M146 SDK 通过 WEBRTC_SDK_DIR 接入。
- Windows Core Audio ADM 设备枚举和真实采集通过。
- 1v1 P2P 双向音频、第三人切 SFU、静音停发已通过短链路验证。
- 默认播放设备的 WASAPI shared-loopback 系统音采集已通过双端验证。
- 下一步完成收听/音量听音、设备异常和离房重进验收。

S1 后剩余工作：

- 信令、认证和房间生命周期。
- libwebrtc 音频、视频发布/订阅和 P2P/SFU。
- 远端视频、屏幕共享、设备热插拔和诊断。
- muxPlayer WGL/D3D11 零拷贝后端启用。
- 安装包、发布验收和生产稳定性测试。
## 2. 第一里程碑范围

只完成以下链路，不接信令、不接 WebRTC、不做远端视频：

```text
Windows Camera
  -> Media Foundation enumeration/capture
  -> NV12
  -> I420 frame pool
  -> rtcwin_engine C ABI local frame callback
  -> Client Gateway UI-thread frame bridge
  -> muxPlayer RawFrameInjector
  -> muxPlayer Live render
  -> GLPlayWidget
  -> PrejoinPage local preview
```

本里程碑成功时：

- 入房页能列出真实摄像头名称。
- 选择设备后可启动/停止本地预览。
- muxPlayer 显示真实摄像头画面。
- 支持 1280x720、约 15fps。
- 连续开关 100 次无崩溃、无设备泄漏。
- 关闭预览后摄像头可被其他程序立即重新打开。
- Debug/Release 构建和测试通过。
- 生成真实预览截图和测试报告。

明确不在第一里程碑：

- 信令连接
- WebRTC 发布/订阅
- 远端视频
- 屏幕共享
- 音频设备枚举
- 安装包

## 3. 关键架构决策

### 3.1 muxPlayer 的位置

- muxPlayer 运行在 `rtc-talk.exe` UI 进程。
- muxPlayer 使用 `/MD`，与 Qt UI 一致。
- `rtcwin_engine.dll` 保持纯媒体引擎，不链接 Qt 或 muxPlayer。
- 摄像头采集在 Engine，视频显示在 UI 的 muxPlayer。

### 3.2 第一版渲染路径

首选：

```text
I420 CPU frame
  -> muxPlayer RawVideoSource
  -> muxPlayer Live RenderScheduler
  -> SoftGLDisplay
  -> GLPlayWidget YUV shader
```

原因：

- 摄像头输出可以直接转换为 I420。
- 不经过 H.264 二次编码。
- 不依赖 NVIDIA WGL interop。
- OpenGL 3.3 即可运行。
- 后续可增加 D3D11 上传/共享纹理路径，不需要改 UI 页面。

备用路径：

```text
I420 CPU frame
  -> muxPlayer RawVideoSource
  -> D3D11FrameRenderer CPU upload
```

首版不实现 CUDA，不使用 QImage 作为生产兜底。

### 3.3 原始帧所有权

- C ABI 帧指针只在回调期间有效。
- Engine 使用有界 I420 frame pool。
- Gateway 在回调内复制或取得带引用计数的 frame lease。
- UI 每路只保留最新帧。
- muxPlayer 渲染完成前不得回收 frame。
- 禁止把 WebRTC/media 线程指针直接交给 QWidget。

### 3.4 muxPlayer 来源

只读来源：

```text
D:\work\device_tool\muxPlayer
device_tool@4c3da7bf8ec92c0826e143918403cba5d61ab1db
```

新工程处理方式：

```text
third_party/muxplayer/
  VERSION.md
  PATCHES.md
  source/
  patches/
```

记录文件清单和 SHA256，不直接依赖 device_tool 工作树。

## 4. 模块变更

### 4.1 `third_party/muxplayer`

任务：

- 固定 muxPlayer 源码快照。
- 拆分或补齐独立 CMake，去掉 GLOB。
- 构建 `muxPlayer.dll`，运行时 `/MD`。
- 锁定 FFmpeg、mtAudio、mttool、log-cpp、OpenSSL、cpr 等依赖版本。
- 只启用首版需要的模块。

### 4.2 `libs/muxplayer_adapter`

新增目标：

```text
rtcwin_muxplayer_adapter
```

职责：

- muxPlayer 初始化/关闭。
- Raw frame injector。
- muxPlayer session 生命周期。
- GL/D3D 能力检测。
- GPU 选择。
- 渲染状态和统计数据。
- 向 UI 暴露稳定接口，不暴露 muxPlayer 私有头文件。

预计文件：

```text
mux_player_environment.h/.cpp
raw_frame_injector.h/.cpp
mux_player_session.h/.cpp
mux_player_surface.h/.cpp
mux_render_capabilities.h/.cpp
```

对外接口：

```cpp
class MuxPlayerSurface final : public QWidget {
 public:
  bool Open(const MuxSurfaceOptions&);
  void SubmitFrame(FrameLease frame);
  void SetMirrored(bool);
  void Clear();
  void Close();
};
```

### 4.3 muxPlayer 补丁

必须新增：

```cpp
enum class RawVideoMemoryType { CpuI420, CpuNv12, D3D11Texture };

struct RawVideoFrame {
  uint64_t frame_id;
  int64_t pts_ms;
  int width;
  int height;
  RawVideoMemoryType memory_type;
  RawVideoPlane planes[3];
  void* native_texture;
  uint32_t subresource;
};

bool MultiplexPlayer::openRawVideo(const RawVideoOpenOptions&,
                                   PlayerStatusCallback, void*);
bool MultiplexPlayer::pushRawVideoFrame(const RawVideoFrame&);
```

实现要求：

- `openRawVideo` 只创建渲染管线，不创建解码器/解封装器。
- 使用 Live 模式，帧到即渲染。
- latest-frame queue 深度为 1。
- `pushRawVideoFrame` 非阻塞。
- `close()` 幂等，重复调用安全。
- close 后拒绝新帧。
- 回调通知只在对象存活时执行。
- 不改变现有 open/openFlv/injectExternalStream 行为。

### 4.4 `libs/camera_win`

新增目标：

```text
rtcwin_camera_win
```

对外接口：

```cpp
class ICameraService {
 public:
  virtual std::vector<CameraDeviceInfo> Enumerate() = 0;
  virtual std::unique_ptr<ICameraCaptureSession> Open(
      const CameraOpenOptions&) = 0;
};

class ICameraCaptureSession {
 public:
  virtual bool Start(CaptureCallback, ErrorCallback) = 0;
  virtual void Stop() = 0;
  virtual CameraCaptureState State() const = 0;
};
```

实现要求：

- Media Foundation 枚举摄像头。
- 稳定设备 ID，不使用 UI 序号作为长期标识。
- 设备名、分辨率、FPS 能力。
- 独立 COM MTA 采集线程。
- 默认协商 1280x720 / 15fps。
- 不支持时选择最接近的偶数分辨率。
- NV12 -> I420。
- 有界 frame pool。
- 权限、占用、拔插、格式错误分类。
- Start/Stop 幂等，连续开关安全。

### 4.5 Engine

新增本地摄像头命令：

```text
camera.devices
camera.preview.start
camera.preview.stop
camera.preview.select
```

命令参数：

```json
camera.preview.start: { "device_id": "...", "width": 1280, "height": 720, "fps": 15 }
camera.preview.stop: {}
```

事件：

```text
camera.devices.changed
camera.preview.state { state, device_id, width, height, fps }
camera.preview.error { code, message, detail }
```

本地帧通过 `rtcwin_options.on_local_frame` 回传。

Engine 首版不依赖 WebRTC，摄像头模块可独立测试。

### 4.6 Client Gateway

任务：

- 接通 `on_local_frame` 回调。
- 将 I420 frame 转为 FrameLease。
- QueuedConnection 投递到 UI 线程。
- 按 `mid`/source 路由到 muxPlayer Surface。
- 处理关闭后迟到帧。
- 连接 `camera.devices` 结果到 PrejoinPage。
- 连接预览状态和错误。

### 4.7 UI

PrejoinPage 改动：

- 摄像头下拉框启用真实设备。
- 显示设备能力。
- 刷新设备按钮。
- 启动/停止预览按钮接真实命令。
- 预览区域使用 `MuxPlayerSurface`。
- 状态显示启动中、预览中、错误。
- 设备被占用、权限拒绝时有明确提示。

房间页本轮不改远端视频逻辑，只保留后续接入点。

## 5. 分阶段任务

### P0：计划与依赖审计（0.5 天）

任务：

- 确认 muxPlayer 源码版本和许可证。
- 确认 FFmpeg/mtAudio/mttool 依赖版本。
- 固定 muxPlayer 目录快照。
- 记录 Windows/GPU/OpenGL 环境。
- 确认摄像头测试设备。

交付：

- `third_party/muxplayer/VERSION.md`
- 依赖清单
- 本地设备测试基线

验收：

- 依赖来源明确，无读取 device_tool 活动工作树作为构建依赖。

### P1：muxPlayer 可构建（1 天）

任务：

- 引入 muxPlayer 源码。
- 编写独立 CMake target。
- 完成 `/MD` 构建。
- 输出 `muxPlayer.dll`。
- 在 UI 进程完成空白 GLPlayWidget 初始化和关闭。
- windeploy 所需 DLL 到构建输出目录。

交付：

- `muxPlayer.dll`
- `rtcwin_muxplayer_adapter`
- 空白视频窗口组件

验收：

- Debug/Release 构建通过。
- 空白 GL widget 打开/关闭 100 次无崩溃。
- 不依赖 D3D 解码路径即可显示固定颜色。

### P2：muxPlayer 原始帧注入（1.5 天）

任务：

- 增加 RawVideoFrame/RawVideoSource API。
- 增加 latest-frame queue。
- 接入 Live RenderScheduler 和 SoftGLDisplay。
- 实现 openRawVideo/pushRawVideoFrame/close。
- 创建合成 I420 测试帧。
- 增加 stride、尺寸、内存所有权校验。

交付：

- muxPlayer raw frame patch。
- 合成 I420 渲染测试。
- adapter 单元测试。

验收：

- 1280x720 I420 测试画面连续渲染。
- 30fps 合成帧队列不积压。
- close 后不再回调。
- 重复 close 安全。
- 画面尺寸/stride 不正确时拒绝并不崩溃。

### P3：本地摄像头采集与 C ABI（1.5 天）

任务：

- 实现 Media Foundation 摄像头枚举。
- 实现采集会话和 NV12->I420。
- 增加 camera.* 命令。
- 本地 I420 frame callback。
- 设备占用/权限错误映射。
- 设备热插拔基础处理。

交付：

- `rtcwin_camera_win`
- Engine camera 命令
- C ABI 本地帧
- camera probe 测试工具

验收：

- 本机摄像头列出。
- 开启后稳定 1280x720/15fps。
- 停止后可被其他程序重新打开。
- 连续开关 100 次通过。
- 错误可恢复，不崩溃。

### P4：UI 本地预览联调（1 天）

任务：

- PrejoinPage 填充真实摄像头。
- 启动/停止按钮接 Engine。
- muxPlayer Surface 接 Gateway 帧。
- 状态和错误提示。
- 镜像、宽高比、DPI。

交付：

- 真实本地预览界面
- 截图
- 运行说明

验收：

- 入房页显示真实画面。
- 启动延迟小于 2 秒。
- 1280x720 画面比例正确。
- 125%/150% DPI 不裁切。
- 停止预览立即清屏。

### P5：稳定性与提交验收（1 天）

任务：

- 10 分钟预览稳定性。
- 连续启动/停止 100 次。
- 设备切换。
- 摄像头被占用。
- 权限拒绝。
- OpenGL 不可用降级测试。
- 内存、句柄、线程检查。
- Debug/Release 全量回归。

验收：

- 无崩溃、无死锁、无持续内存增长。
- 关闭后摄像头句柄完全释放。
- 日志可定位设备、帧率和错误。
- 生成测试报告和截图。

第一里程碑总预估：**5.5–6.5 个有效工作日**。

## 6. 测试计划

### 6.1 单元测试

- I420 尺寸/stride 校验。
- latest-frame queue 丢旧保新。
- muxPlayer session open/close 幂等。
- 摄像头错误码映射。
- C ABI 参数校验。
- 本地帧 generation 隔离。

### 6.2 组件测试

- 合成 I420 -> muxPlayer -> Surface。
- 摄像头枚举。
- 摄像头采集 -> I420 callback。
- 100 次启动/停止。
- 预览停止后设备释放。

### 6.3 集成测试

- rtc-talk.exe -> Gateway -> Engine -> Camera -> Frame callback -> muxPlayer。
- 真实摄像头预览。
- 错误路径。

### 6.4 人工验收

- 画面正确、无明显延迟。
- 设备名正确。
- 镜像正确。
- 停止按钮立即生效。
- 重新启动预览正常。
- 应用关闭后摄像头指示灯熄灭。

## 7. 性能指标

| 指标 | 目标 |
|---|---|
| 设备枚举 | < 3 秒 |
| 预览首帧 | < 2 秒 |
| 采集帧率 | 15fps，允许 12–16fps 波动 |
| UI 刷新 | 30fps 合并上限 |
| 端到端延迟 | < 150ms（不含显示刷新） |
| 10 分钟内存增长 | 可解释且稳定 |
| 连续开关 | 100 次无失败 |
| 空闲 CPU | 预览关闭时接近 0 |

## 8. 风险与处理

| 风险 | 处理 |
|---|---|
| muxPlayer 当前不支持原始帧 | P2 先完成 RawVideoFrame API |
| muxPlayer 源码依赖复杂 | P0 固定源码和依赖，P1 单独构建验证 |
| Qt `/MD` 与 muxPlayer 依赖不匹配 | muxPlayer 只在 UI 进程，独立 target |
| 摄像头权限/占用 | 强类型错误，可切换设备，不退出应用 |
| 帧生命周期错误 | FrameLease + 回调内复制/引用计数 |
| OpenGL 兼容性 | 先 SoftGL，保留 D3D11 fallback |
| 多实例渲染资源 | 第一版单预览 session，后续做 pool |
| 连续开关死锁 | 明确 Start/Close owner 线程和 join 顺序 |
| 设备序号变化 | 使用稳定 DeviceId，不持久化 index |

## 9. 第一里程碑完成定义

只有以下全部满足才进入信令/WebRTC 开发：

1. muxPlayer 已从固定源码版本成功构建。
2. 原始 I420 帧注入可用。
3. 本机摄像头能在入房页列出。
4. muxPlayer 显示真实摄像头画面。
5. 启动、停止、重复启动和设备切换可用。
6. CTest 全部通过。
7. Debug/Release 构建通过。
8. 100 次开关和 10 分钟稳定性通过。
9. 关闭预览后摄像头资源释放。
10. 用户完成真实画面验收。

## 10. 后续阶段（本轮不执行）

第一里程碑通过后，按顺序进入：

1. 信令连接、hello、room join/leave。
2. WebRTC 音频发布/订阅。
3. 摄像头和屏幕共享发布。
4. 远端视频 I420 帧注入 muxPlayer。
5. 多路 muxPlayer session pool、宫格和共享布局。
6. PTT、发言权和主持人。
7. 设备、统计、诊断和安装包。

本轮只执行 P0–P5，完成本地摄像头 + muxPlayer 真实联调。

## 11. 全项目开发周期总计划

### 11.1 能否确定完整计划

可以确定完整开发阶段、依赖关系、交付物和验收门禁，但不能在没有完成以下技术验证前承诺固定发布日期：

1. muxPlayer 在 `/MD` UI 进程中的完整构建和运行。
2. muxPlayer 原始 I420 帧注入补丁。
3. 本机摄像头设备、权限和 OpenGL 渲染链路。
4. 服务端 WSS、信令和媒体链路是否已达到生产可用状态。
5. libwebrtc SDK 与 Qt/Engine 的 CRT 和线程集成。

因此本计划采用：

- 阶段固定。
- 顺序固定。
- 交付物固定。
- 验收标准固定。
- 工作量使用区间。
- 每个阶段完成后重新校准剩余工期。

### 11.2 总体工作量

| 阶段 | 内容 | 基础工作量 |
|---|---|---:|
| S0 | 需求、架构和基线确认 | 0.5 天 |
| S1 | muxPlayer + 本地摄像头预览 | 5.5–6.5 天 |
| S2 | 信令、认证、入房和离房 | 3–4 天 |
| S3 | WebRTC 音频与媒体链路 | 4–5 天 |
| S4 | 摄像头/屏幕发布与远端 muxPlayer 渲染 | 4–5 天 |
| S5 | 成员、发言权、PTT 和主持人 | 3–4 天 |
| S6 | 设备、统计、日志和诊断 | 3–4 天 |
| S7 | 重连、安全、异常和稳定性 | 2.5–3.5 天 |
| S8 | 安装包、干净机验证和发布 | 2.5–3.5 天 |

基础合计：**28–36.5 个有效人日**。

考虑 muxPlayer 依赖、设备差异、服务端联调和现场问题，建议增加 15%–20% 风险缓冲：

**最终计划区间：32–44 个有效人日，约 7–9 个工作周。**

以上估算假设：

- 一名主要开发者全职投入。
- 服务端基本可用。
- 一台带摄像头和可用 OpenGL 3.3 的 Windows 测试机。
- 不包含服务端功能开发。
- 不包含多平台客户端。
- 不包含产品需求大幅变更。

### 11.3 周级排期建议

| 周次 | 目标 | 主要交付 |
|---|---|---|
| 第 1 周 | muxPlayer 和本地摄像头 | S1 完成并通过真实预览验收 |
| 第 2 周 | 信令和房间生命周期 | S2 完成，真实服务入房/离房通过 |
| 第 3 周 | WebRTC 音频和基础媒体 | S3 完成，1v1 音频通话通过 |
| 第 4 周 | 视频发布和远端 muxPlayer | S4 完成，P2P/SFU 视频互看通过 |
| 第 5 周 | 房间业务 UI | S5 完成，PTT 和主持人功能通过 |
| 第 6 周 | 设备、统计和诊断 | S6 完成，设备和状态页可用 |
| 第 7 周 | 稳定性、重连和安全 | S7 完成，长稳和异常场景通过 |
| 第 8–9 周 | 打包和发布 | S8 完成，干净机验收通过 |

每周结束必须完成回归，不允许带着未解决的核心缺陷进入下一周。

## 12. 全阶段详细计划

### S0：计划与基线（0.5 天）

交付：

- 本计划确认。
- 当前代码基线 `d49836f`。
- muxPlayer 版本、SDK、Qt、服务端版本记录。
- 测试设备和摄像头记录。

验收：

- 计划、依赖和测试环境明确。
- 未确认事项列明。
- 后续工作不依赖 demo 活动工作树。

### S1：muxPlayer + 本地摄像头（5.5–6.5 天）

范围：

- muxPlayer 固定源码构建。
- Raw I420 frame 注入。
- Media Foundation 摄像头枚举和采集。
- Engine C ABI 本地帧。
- PrejoinPage 设备选择和 muxPlayer 预览。

验收：

- 真实摄像头预览可用。
- 1280x720/15fps。
- 100 次开关通过。
- 10 分钟稳定。
- 停止后设备释放。
- 截图和测试报告完整。

S1 未通过，不进入 S2。

### S2：信令、认证和房间生命周期（3–4 天）

交付：

- WSS/WS Signaling Transport。
- TLS 证书校验。
- `session.hello`、认证、心跳。
- `room.join`、`room.leave`。
- 命令 ID、超时和结果关联。
- generation 隔离。
- prejoin 到 room 的真实页面切换。
- 断线状态骨架。

验收：

- 真实服务连接、入房、离房。
- 两客户端成员列表相互可见。
- Token 错误、房间不存在、重复入房有稳定错误码。
- 主动离房不重连，网络断开进入明确状态。
- 单元测试和真实服务冒烟通过。

### S3：WebRTC 音频与基础媒体链路（4–5 天）

交付：

- libwebrtc SDK 集成到 Engine。
- Engine 控制线程和媒体线程边界。
- WASAPI 录音与播放。
- Opus 音频发布/订阅。
- SFU send/recv PeerConnection。
- P2P 基础链路。
- 静音、音量和收听。
- 媒体事件和错误映射。

验收：

- 1v1 P2P 音频双向通话。
- 第三人加入后切换 SFU 音频继续。
- 静音和音量生效。
- 媒体设备错误不导致崩溃。
- 关闭和重连不泄漏 PeerConnection。

### S4：视频发布与远端 muxPlayer 渲染（4–5 天）

当前进度（S4.1，已完成短链路验证）：

- `AudioMediaSession` 已重构为 `MediaSession`，音频和摄像头共用同一 SFU
  PeerConnection，避免独立视频 transport 的 DTLS/mediasoup 绑定冲突。
- 注入 Media Foundation H.264 单层编码器和解码器，固定
  `packetization-mode=1 / profile-level-id=42e01f`，与 SFU Router 一致。
- Engine 将 `camera_win` 的 I420 帧送入 `AdaptedVideoTrackSource`，切换
  `media.setMute{source:"camera"}` 控制 producer 与本地 sender。
- 解析 `sfu.offer.consumers[]` 的 `mid/uid/kind/source`，远端 I420 通过
  版本化 C ABI 回到 Qt UI，按 `uid + source` 注入 muxPlayer。
- 房间页的视频 tile 已使用 `MuxVideoSurface`，不再用 QImage 作为远端视频
  生产渲染路径。
- `log-cpp[shared]@1.0.0#4` 改由私有 vcpkg registry 提供；新增
  `rtcwin_log_adapter.dll` C ABI 边界，避免 `/MT` Engine 与 `/MD` log-cpp
  跨 CRT 传递 STL 类型。
- Release/Debug 全量构建通过，CTest 5/5 通过（新增状态机测试）。
- 测试服双客户端验证：发送端 `camera_frames=31 -> 179`、
  `video_packets_sent=14 -> 293`；接收端
  `video_packets_received=53 -> 325`，远端摄像头画面正常显示。

仍未完成：

- 多路视频 session pool、9 路性能、5 秒无帧回收和长时间稳定性。
- 后续把 `client_gateway` 的 C ABI 解析、HTTP 和快照逻辑继续内聚拆分。

S4.1 后的重构进度：

- B1 Command/Event Facade 已完成。
- `EngineCommandRouter` 统一命令注册和分发。
- `EngineEventSink` 统一 C ABI 响应/通知信封。
- 所有 Engine 命令与事件 JSON 改用 nlohmann，已删除手拼 JSON 和字符串解析。
- B2 Signaling Transport 已完成。
- `SignalingTransport` 独占 io_context、信令线程、SignalingClient 和停止生命周期。
- B3 Session State Machine 已完成。
- `SessionStateMachine` 用显式 phase/media mode 和 generation 管理连接、入房、
  离房、失败与媒体就绪状态；旧回调不能污染新会话。
- B4 Media Orchestrator 已完成。
- EngineCore 已降至约 198 行；`SessionController`、`MediaOrchestrator`
  分离了会话生命周期和媒体编排。
- B5 MediaSession 内聚拆分已完成。
- `AudioPipeline`、`VideoPipeline`、`SdpNegotiator`、`MediaPeer`、
  `RemoteTrackRegistry`、`StatsCollector` 已独立。
- S4.2 屏幕共享已完成：WGC/DXGI 桌面采集、BGRA→I420、SFU screen producer、
  P2P screen m-line、停止共享清屏均通过双端截图验收。
- S4.2 P2P 视频已完成：audio/camera/screen 三条 m-line，P2P 摄像头和屏幕共享
  均已完成双端截图验收。

交付：

- 摄像头和屏幕共享轨道发布。
- 远端视频 track 注册。
- I420 frame + frame lease。
- muxPlayer session pool。
- camera/screen 分流。
- 宫格、聚焦、1v1 画中画、共享布局。
- producer paused 立即清屏。
- 5 秒无帧回收。
- 本地预览和正式发布独立生命周期。

验收：

- 双人 P2P 摄像头和共享互看。
- 第三人加入后切 SFU 视频继续。
- 多路 muxPlayer 渲染无串流。
- 停止共享立即清屏。
- 预览停止不影响发布。
- 9 路视频性能测试可接受。

### S5：成员、发言权和主持人（3–4 天）

交付：

- 成员状态、音量和主讲人。
- 自由发言、PTT、moderated。
- 发言权申请、排队、释放、revoke。
- 主持人授予/收回。
- 全员静音、移出成员。
- 房间策略局部更新。
- 主持人策略弹窗。

验收：

- PTT 排队和递补正确。
- 被收回发言权后立即闭麦。
- host 策略更新不重置其他字段。
- 全员静音和踢人流程正确。
- 音量、发言人和排队 UI 与服务端一致。

### S6：设备、统计、日志和诊断（3–4 天）

交付：

- 音频录制/播放设备枚举与选择。
- 摄像头设备选择。
- 设备热插拔。
- `media.stats` 周期采集。
- 状态面板显示 RTT、码率、丢包、设备。
- 日志滚动和脱敏。
- 诊断包导出。

验收：

- 设备切换和失败回滚正确。
- 状态面板数据与核心一致。
- 诊断包不包含 Token、完整 SDP 等敏感信息。
- 日志能定位连接、设备、媒体和视频问题。

### S7：重连、安全和稳定性（2.5–3.5 天）

交付：

- 断线重连、resume、恢复过期。
- 顶号、被踢、主动离房分流。
- WSS 证书错误处理。
- 命令超时和取消。
- 错误恢复和 Toast/错误中心。
- 30 分钟双人、2 小时多人测试框架。
- 连续设备切换、连续视频开关。
- 资源泄漏和关闭顺序修复。

验收：

- 断网 10 秒后可恢复或给出明确失败路径。
- kicked/replaced 不自动重连。
- 2 小时多人视频无崩溃、无明显资源增长。
- 连续开关摄像头/共享 100 次通过。
- 关闭应用后无残留线程和摄像头句柄。

### S8：安装包与发布验收（2.5–3.5 天）

交付：

- ReleaseWithSymbols 构建。
- windeployqt。
- VC Runtime、Qt 插件、muxPlayer DLL、FFmpeg 依赖。
- 版本信息、图标和许可证清单。
- 安装、升级、卸载。
- 崩溃转储路径。
- 运行手册、故障排查和发布说明。
- 干净 Windows 10/11 机器验证。

验收：

- 干净机器可安装和启动。
- 连接、音频、视频、共享、PTT、主持人和离房通过。
- 无缺失 DLL。
- 无调试工具和 demo 文件进入主安装目录。
- 完整回归矩阵通过。

## 13. 跨阶段并行工作

以下工作贯穿全部阶段：

### 13.1 构建与 CI

- Debug/Release 每阶段构建。
- 编译警告清零目标。
- 单元测试和真实冒烟测试。
- 架构依赖检查。
- 包依赖版本锁定。

### 13.2 测试资产

- Fake signaling transport。
- Fake camera/audio/screen。
- 合成 I420 帧源。
- 双客户端测试脚本。
- muxPlayer 渲染探针。
- 设备错误场景。

### 13.3 文档

- 接口变更记录。
- 集成基线。
- 运行手册。
- 故障码表。
- muxPlayer patch 说明。

## 14. 关键依赖和阻塞条件

| 依赖 | 最晚需要时间 | 影响 |
|---|---|---|
| muxPlayer 可构建 | S1 开始前 | 整个视频架构阻塞 |
| Raw frame API 完成 | S1 中 | 本地预览和远端视频阻塞 |
| 本机摄像头权限 | S1 开始前 | 预览验收阻塞 |
| WSS 服务可用 | S2 开始前 | 正式信令阻塞 |
| libwebrtc SDK | S3 开始前 | 音频和视频阻塞 |
| SFU/TURN 可联调 | S3 中 | P2P/SFU 验收阻塞 |
| 第二台测试客户端 | S4 开始前 | 双端视频验收阻塞 |
| 安装机/干净机 | S8 开始前 | 发布验收阻塞 |

任一关键依赖延期，后续阶段的开始时间顺延；不允许用假实现替代验收。

## 15. 阶段门禁

每个阶段必须同时满足：

1. 代码完成。
2. Debug/Release 构建通过。
3. 单元和组件测试通过。
4. 真实链路或设备测试通过。
5. 资源释放和错误路径通过。
6. 日志和诊断信息充分。
7. 用户验收确认。

未通过门禁，不进入下一阶段。

## 16. 最终发布条件

- S1–S8 全部完成。
- 所有 CTest 和真实服务测试通过。
- P2P、SFU、摄像头、共享、PTT、主持人、重连全部验收。
- 125%/150% DPI 通过。
- 30 分钟双人和 2 小时多人稳定性通过。
- 干净 Windows 10/11 安装和运行通过。
- 诊断包、日志和安装包可交付。
- 用户完成最终验收。

## 17. 计划维护

每完成一个阶段，更新：

- 实际工作量。
- 剩余估算。
- 风险状态。
- 集成基线提交号。
- 测试和截图结果。

若阶段偏差超过 20%，必须重新评审后续排期，但不得降低阶段验收标准。
