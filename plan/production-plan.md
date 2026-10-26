# rtc-talk-win 生产开发计划

> 状态：待用户确认；确认前不开始业务代码。  
> 日期：2026-10-26  
> 需求基线：`docs/requirements.md`  
> 工程目录：`D:\google\webrtc-win-client\rtc-talk-win`  
> 独立 Git 仓库初始分支：`rtc-talk-win`

## 1. 仓库与工程决策

目标目录位于现有 `rtc-talk` 仓库之外，因此本工程作为**独立 Git 仓库**管理，
初始分支为 `rtc-talk-win`。现有 `rtc-talk` 仓库只读，不修改、不切换、
不提交、不合并。

首批提交前建立以下基线文件：

```text
rtc-talk-win/
  .gitignore
  .editorconfig
  CMakeLists.txt
  CMakePresets.json
  vcpkg.json
  vcpkg-configuration.json
  README.md
  docs/
    requirements.md
    architecture.md
    coding-standard.md
    adr/
  plan/
    production-plan.md
```

如果用户原意是“在旧 `rtc-talk` 仓库创建分支”，而不是独立仓库，应在编码前停止并调整；
当前按目标目录和“新工程”描述选择独立仓库。

## 2. 只读参考基线

| 参考 | 用途 | 约束 |
|---|---|---|
| `rtc-talk@bbb3891` | 已验证过的行为样本、联调场景 | 只读 |
| `client-win` demo | 协议能力、设备能力、历史缺陷 | 只读 |
| `qt-ui-prototype` | 视觉、布局、QSS 参考 | 只读 |
| 服务端 `origin/main` | 协议权威实现 | 只读，联调前重新确认提交号 |
| `device_tool/omni_station` | Qt 工程结构、无边框、视频窗口参考 | 只读 |
| `D:/google/webrtc_sdk_hevc` | WebRTC SDK | 只链接，不修改 |

联调开始时在 `docs/integration-baseline.md` 记录实际服务端提交、SDK 版本和
工程版本；未记录基线不得声明集成通过。

## 3. 代码来源规则

1. demo 只提供需求和回归场景，不作为生产代码结构模板。
2. 复用算法时，先写成独立 adapter/utility，再补接口、测试、错误处理和生命周期。
3. 禁止跨模块 include demo 私有头文件。
4. 禁止把 demo 的大型 `WebRtcMedia`、`CallSession` 原样复制后继续扩展。
5. 每个迁移能力建立“行为对照表”：输入、输出、事件、失败路径、线程约束。
6. 代码评审发现 demo 结构泄漏时，该模块不得验收。

## 4. 目标架构

```text
┌─────────────────────────────────────────────────────────────┐
│ apps/rtc-talk                                               │
│ Bootstrap · DI · lifecycle · single-instance · crash guard  │
└──────────────────────────────┬──────────────────────────────┘
                               │ application interfaces
┌──────────────────────────────▼──────────────────────────────┐
│ ui/                                                         │
│  shell · pages · widgets · video surfaces · models · theme  │
└──────────────────────────────┬──────────────────────────────┘
                               │ typed state / commands
┌──────────────────────────────▼──────────────────────────────┐
│ application/                                                │
│  SessionService · DeviceService · SettingsService           │
│  DiagnosticsService · ViewModels · state reducers           │
└──────────────────────────────┬──────────────────────────────┘
                               │ domain ports
┌──────────────────────────────▼──────────────────────────────┐
│ domain/                                                     │
│  Session · Room · Member · Floor · Policy · MediaStream     │
│  Device · Diagnostics · Result/Error · typed events         │
└──────────────────────────────┬──────────────────────────────┘
                               │ ports
┌──────────────────────────────▼──────────────────────────────┐
│ infrastructure/                                             │
│  signaling · WebRTC adapter · camera · audio · screen      │
│  Windows settings/credential · logging · diagnostics        │
└──────────────────────────────┬──────────────────────────────┘
                               │ versioned C ABI
┌──────────────────────────────▼──────────────────────────────┐
│ rtc_engine.dll                                              │
│  Signaling codec · session engine · media orchestration     │
└──────────────────────────────┬──────────────────────────────┘
                               │
                    libwebrtc / Windows devices
```

### 4.1 依赖方向

```text
ui -> application -> domain
application -> domain
infrastructure -> domain
apps -> ui + application + infrastructure
domain -> standard library only
```

禁止反向依赖：

- `domain` 不得依赖 Qt、WebRTC、Windows SDK、JSON。
- `ui` 不得依赖 `infrastructure` 具体实现。
- `application` 不得依赖 PeerConnection、Media Foundation 或 QWidget。
- `infrastructure` 不得操作 QWidget。

### 4.2 关键端口

```cpp
class ISignalingTransport;
class ISessionEngine;
class IAudioService;
class ICameraService;
class IScreenService;
class IPreviewSession;
class IVideoPublisher;
class IRemoteVideoStreams;
class IMediaStatistics;
class IClock;
class ICredentialStore;
```

具体命名在 W2 通过接口评审后定稿。接口必须满足：

- 强类型输入输出。
- 明确同步/异步语义。
- 提供 cancellation/generation。
- 可注入 Fake 实现。
- 不暴露 JSON、QWidget、WebRTC 或 COM 类型。

## 5. 目录结构

```text
rtc-talk-win/
  apps/
    rtc-talk/
      app_bootstrap.*
      main.cpp
  libs/
    domain/
    application/
    protocol/
    client_api/
      include/rtc/client_api.h
    platform_win/
    camera_win/
    audio_win/
    screen_win/
    webrtc_media/
    engine/
    ui/
      shell/
      components/
      pages/
      video/
      theme/
      models/
  tests/
    unit/
    integration/
    smoke/
    fixtures/
  tools/
    talk-cli/
    camera-probe/
  packaging/
    windows/
  scripts/
  docs/
  plan/
```

### 5.1 CMake 目标

| 目标 | 职责 | 运行库 |
|---|---|---|
| `rtc_domain` | 纯领域类型和端口 | `/MD` |
| `rtc_protocol` | 协议编解码、校验、错误码 | `/MT` 或静态无 CRT 依赖 |
| `rtc_client_api` | 版本化 C ABI | 边界 |
| `rtc_camera_win` | MF/WGC 摄像头采集与枚举 | `/MT` |
| `rtc_audio_win` | WASAPI/测试音频适配 | `/MT` |
| `rtc_screen_win` | 桌面采集适配 | `/MT` |
| `rtc_webrtc_media` | PeerConnection、SDP、轨道、帧转换 | `/MT` |
| `rtc_engine` | 会话和媒体编排 DLL | `/MT` |
| `rtc_application` | 应用服务、ViewModel、状态收敛 | `/MD` |
| `rtc_ui` | Qt Widgets 组件、页面、视频显示 | `/MD` |
| `rtc-talk` | 生产可执行程序 | `/MD` |

## 6. 线程与生命周期

```text
UI thread
  QWidget / ViewModel / presentation state
        ▲ queued typed events
        │
Engine control thread
  signaling / session state machine / command dispatch
        ▲
        │
Media threads
  camera capture / desktop capture / WebRTC worker/signaling
        │
        ▼ frame copies only
UI thread
```

规则：

- 跨线程只传值类型或明确所有权对象。
- 回调中禁止销毁对象、阻塞、操作 QWidget 或抛异常。
- 所有异步请求有超时和取消。
- 每路视频只保留最新帧，Surface 按约 33ms 合并刷新。
- 每代会话有 generation，旧回调不得污染新会话。
- 关闭顺序为：停止 UI 命令 -> 停止发布/预览 -> 停止信令 -> 关闭媒体 -> 关闭引擎。

## 7. 分阶段交付

每个阶段完成后暂停，提交验证清单、构建结果、测试结果和截图；用户确认后才进入下一阶段。

### W0：仓库与架构骨架（1 天）

交付：

- 独立 Git 仓库和 `rtc-talk-win` 分支。
- CMake/CMakePresets/vcpkg 配置。
- 空目标依赖图、目录、编码规范、ADR 模板。
- `rtc-talk` 可启动空窗口，不接入真实业务。
- CI 本地脚本：configure、build、test。

验收：

- Debug/Release 均可构建。
- 依赖方向检查脚本可运行。
- UI target 不包含 WebRTC 私有头文件。

### W1：平台基础能力（1.5 天）

交付：

- 日志、路径、版本、配置、凭证、错误码框架。
- Token 内存与 Windows 凭证存储接口。
- 崩溃转储和诊断包目录预留。
- 单实例、DPI、应用生命周期。

验收：

- 日志滚动和脱敏测试。
- 配置往返测试。
- Token 不落普通配置文件。
- 日志初始化失败不导致应用崩溃。

### W2：领域契约与协议层（2 天）

交付：

- `domain` 强类型状态和端口。
- `rtc_protocol` JSON codec、完整错误码和字段校验。
- 请求/响应/通知信封测试。
- Fake clock/transport/event sink。

验收：

- 未知字段容忍、非法消息拒绝。
- 领域层不包含 JSON/Qt/WebRTC。
- 协议 fixture 测试覆盖 join、floor、policy、video、result、error。

### W3：Windows 采集模块（3 天）

交付：

- `CameraService`：枚举、权限、占用、设备索引、热插拔。
- `AudioService`：录制/播放设备、音频源、音量、静音。
- `ScreenService`：主屏选择、采集启停和错误。
- `PreviewSession`：独立预览状态机。
- Fake camera/audio/screen 实现。

验收：

- 摄像头设备权限、占用、拔插测试。
- 预览连续开关 100 次无卡死。
- 预览停止不影响正式摄像头发布。
- 采集模块不依赖 UI 和 Session。

### W4：WebRTC 媒体管线（3 天）

交付：

- PeerConnection 封装仅负责 SDP/ICE/轨道/sender。
- SFU send/recv 与 P2P 管线。
- 摄像头、屏幕、本地预览、远端视频独立 sink。
- 帧队列上限、33ms 合并和无帧回收。
- 媒体统计适配。

验收：

- 双人 P2P 摄像头和共享互看。
- 三人切 SFU 后视频继续。
- producer paused 立即清屏。
- 视频帧不串流、不积压。
- 关闭和重连无死锁。

### W5：会话引擎与应用服务（2.5 天）

交付：

- `SessionEngine` 状态机、generation、命令超时。
- 连接、hello、join、leave、resume、kicked。
- `SessionService`、`DeviceService`、`SettingsService`。
- ViewModel 状态快照。

验收：

- Fake transport 下状态机全覆盖。
- 断线 10 秒恢复、恢复过期、顶号、被踢路径通过。
- 页面不解析协议 JSON。

### W6：生产 UI 框架与入房页（2 天）

交付：

- 无边框窗口、主题、图标、DPI。
- 入房页、设备选择、音量、模式配置。
- 本地预览 Surface。
- 设置持久化和错误提示。

验收：

- 1100x720、1440x900、125%、150% DPI 截图。
- 设备枚举异步，不阻塞 UI。
- 键盘 Tab 顺序和禁用态正确。

### W7：房间 UI、对讲与主持人（3 天）

交付：

- 成员模型、设备/状态侧栏。
- 麦克风、摄像头、共享、收听控制。
- PTT/moderated、排队、倒计时、revoke。
- 主持人策略、授权、全员静音、移出。
- 宫格、聚焦、1v1、共享布局。

验收：

- 双端 PTT 和主持人流程。
- 6 人布局和“仅视频成员”过滤。
- 视频、成员、音量、主讲人同步。
- 失败操作按钮恢复，不出现永久禁用。

### W8：统计、诊断和稳定性（2 天）

交付：

- 周期 stats、状态页、错误中心。
- 日志导出、脱敏诊断包。
- 设备热插拔和切换失败回滚。
- 重连和长时间运行测试。

验收：

- 统计与服务端/core 数据一致。
- 诊断包不含敏感信息。
- 30 分钟双人、2 小时多人稳定性通过。

### W9：安装包与发布验收（2.5 天）

交付：

- windeployqt 打包。
- VC Runtime、Qt 插件、WebRTC DLL、许可证。
- 安装/卸载、版本资源、图标。
- 干净 Windows 10/11 机器验证。
- 运行手册、故障排查、发布说明。

验收：

- 干净机器安装后可连接、入房、通话、视频和离房。
- 无缺失 DLL、无调试残留、无未授权文件。
- 完整回归矩阵通过。

总预估：**21.5 个有效工作日**，不含外部服务阻塞和用户验收等待。

## 8. 测试矩阵

### 8.1 单元测试

- 协议信封和字段校验。
- 命令相关 ID、超时、取消和 generation。
- Session/Room/Floor 状态收敛。
- producer paused、无帧回收和流注册。
- 设备选择、权限错误和热插拔。
- 配置和凭证脱敏。
- ViewModel 状态转换。

### 8.2 集成测试

- connect + hello + join + leave。
- P2P 双人音频/摄像头/共享。
- 第三人切换 SFU。
- PTT 排队、释放、revoke。
- host grant/muteAll/kick/setPolicy。
- reconnect/resume/kicked/replaced。
- 设备失败回滚和重试。

### 8.3 UI 回归

- 入房页。
- 设备面板。
- 1v1 画中画。
- 宫格。
- 聚焦。
- 共享布局。
- 状态/诊断页。
- 策略弹窗。
- 125%/150% DPI。

### 8.4 稳定性

- 连续摄像头/共享开关 100 次。
- 连续设备切换。
- 断网 10 秒。
- 30 分钟双人通话。
- 2 小时多人视频。

## 9. 质量门禁

每个 PR/提交必须通过：

1. Release 和 Debug 构建。
2. CTest 全部通过。
3. 架构依赖检查。
4. 格式和静态检查。
5. 敏感信息扫描。
6. 真实服务冒烟测试。
7. 对应阶段截图或运行证据。
8. 用户阶段确认。

以下情况直接拒绝：

- UI 引用 WebRTC/asio/JSON 私有头文件。
- 页面直接发协议 JSON。
- 新增万能类或超过 600 行且无拆分说明。
- 回调线程和生命周期不清晰。
- 只有 happy path，没有失败路径测试。
- 直接复制 demo 类后继续堆代码。

## 10. 主要风险

| 风险 | 影响 | 对策 |
|---|---|---|
| libwebrtc SDK 与 Qt CRT 不一致 | 崩溃/链接失败 | 保持 `/MT` 引擎 + `/MD` UI，只走 C ABI |
| TLS/WSS 未实现 | 无法正式上线 | W2/W5 明确实现并测试证书校验 |
| 多线程关闭顺序错误 | 退出崩溃/死锁 | 生命周期状态机、关闭顺序测试 |
| 设备权限和占用 | 预览/发布失败 | 强类型错误、可重试、设备 probe |
| P2P/SFU 模式切换 | 视频中断 | 独立媒体管线与模式切换集成测试 |
| Qt QImage 渲染性能 | 多路视频卡顿 | 最新帧合并、Surface 工厂、D3D 预留 |
| 服务端协议演进 | 解析失败 | 协议层集中、未知字段容忍、fixture |
| 安装依赖遗漏 | 干净机器无法启动 | 自动打包清单和干净机验收 |

## 11. 提交与验收流程

```text
需求确认
  -> 计划确认
  -> W0 实现
  -> 构建/测试/架构门禁
  -> 用户验收
  -> W1 ...
```

任何阶段不得跳门禁。若阶段中发现需求或架构冲突：

1. 停止编码。
2. 写 ADR 或需求变更。
3. 更新计划。
4. 用户确认后继续。

## 12. 当前状态

- 已创建独立目录和 `rtc-talk-win` Git 分支。
- 已产出需求基线 `docs/requirements.md`。
- 已产出本生产计划。
- **尚未开始业务代码和工程骨架。**
- 等待用户确认需求与计划，确认后才进入 W0。
