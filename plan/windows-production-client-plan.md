# Windows 生产客户端集成开发计划

> 状态：P0 已完成，等待确认进入 P1；其他分支只允许只读参考  
> 计划日期：2026-10-26  
> 目标：把 `client-win` 的真实媒体/信令能力与 `qt-ui-prototype` 的 UI 组合为面向生产的 Windows 桌面客户端  
> 建议分支：`codex/feat/windows-production-client`

## 1. 当前分支与服务端基线

### 1.1 Git 现状

| 项 | 值 |
|---|---|
| 当前分支 | `codex/feat/windows-production-client` |
| 当前 HEAD | `8b705e3` |
| 远端 main | `origin/main` = `4a24f30` |
| 当前分支与 main 的 merge base | `cd3fef2` |
| 当前工作区 | 干净；UI 原型已保存为基线提交 |

`main` 已经是**纯服务端分支**，其最新差异会删除 `client-win` 目录。所有其他分支只做只读参考，禁止在 `main` 或其他分支上提交、合并、cherry-pick 或改写历史。当前生产开发只在 `codex/feat/windows-production-client` 上进行。

### 1.2 只读参考的最新服务端修复

`origin/main` 新增 `4a24f30`，包含两项会直接影响客户端体验的修复：

1. **音量与主讲人 observer 修复**
   - `AudioLevelObserver` 和 `ActiveSpeakerObserver` 补上 `addProducer`
   - observer 初始化幂等化
   - 已发布 producer 在 observer 就绪后回补
   - 结果：`room.audioLevels` 和 `room.activeSpeaker` 可以产生有效数据

2. **`host.setPolicy` 局部合并修复**
   - 策略输入 schema 改为全部 optional 且无 default
   - `setPolicy` 使用字段级合并，不再重置未提交字段
   - 结果：生产 UI 可以安全实现“只修改一个策略字段”的主持人设置面板

生产客户端联调时必须记录并验证服务端版本 `origin/main@4a24f30`，但客户端分支不合并 `main` 代码。服务端升级与部署由服务端分支独立完成。

## 2. 现有客户端能力调研

`client-win` 当前是功能测试 demo，但核心能力已经具备：

### 2.1 核心库能力

- `talk_core.dll` 通过 `talk/talk.h` 导出稳定 C ABI
- 信令连接、JWT、断线恢复、顶号
- SFU 音频发布/订阅
- P2P / SFU 自动切换
- 摄像头和屏幕共享两路视频上行
- 多路远端视频回调
- 音频设备枚举、采集源切换、播放音量
- PTT、发言权队列、主持人管控、音量与主讲人事件
- 媒体统计、日志和错误事件

### 2.2 当前 Qt demo 能力

`ui/qt/window.cpp` 已实现：

- 连接服务、开发 token、入房/离房
- 麦克风、摄像头、屏幕共享
- SFU/P2P 模式显示
- PTT `pressed/released`
- 发言权状态、倒计时和排队
- 主持人全员静音、授予发言权和移出
- 成员音量条
- 多路远端视频宫格
- 视频轨道元信息和 producer 暂停处理
- 设备、音量、日志和媒体统计

### 2.3 当前 UI 原型能力

`qt-ui-prototype` 已实现：

- 入房页和房间页
- 多人宫格、聚焦、1v1、共享布局
- 成员/设备/状态侧栏
- 设置、离开、诊断弹窗
- 无边框窗口、DPI、截图和场景测试入口

### 2.4 两边之间的主要缺口

| 能力 | demo | UI 原型 | 生产化缺口 |
|---|---|---|---|
| 信令/入房 | 已有 | mock | 用真实 Controller 替换 mock |
| 成员与角色 | 已有 | mock | 事件到 `UiState` 的稳定映射 |
| 麦克风/摄像头/共享 | 已有 | mock | 命令、结果、失败回滚 |
| PTT/主持管控 | 已有 | 部分 UI | 发言权、排队、策略面板 |
| 音量/主讲人 | 已有 | 缺失 | 音量订阅、主讲人高亮 |
| 多路远端视频 | 已有 | 按成员 mock | 按 mid 的流注册表和 Surface |
| 本地视频预览 | 无 | 有 mock | 需要扩展 core 本地预览回调 |
| 摄像头设备选择 | 无 | 有 UI | 需要扩展 core 摄像头设备枚举/选择 |
| call.invite 呼叫 UI | core 未实现 | 无 | 一期不应默认包含 |
| 诊断/stats | 日志为主 | mock JSON | 真实 stats 异步接入 |
| 设置持久化 | QSettings | mock | 生产配置和 token 策略 |

## 3. 里程碑边界

### 3.1 一期生产客户端范围

一期必须完成：

- 真实连接、hello、入房、离房
- 开发 token 和正式 token 输入
- 真实成员、角色和连接状态
- 麦克风、摄像头、屏幕共享、收听
- P2P/SFU 模式切换与视频状态
- 多路远端视频宫格
- PTT、发言权、排队、倒计时、被收回
- 主持人全员静音、授予/收回、移出
- 音量条和主讲人
- 音频设备和播放设备
- 媒体统计和诊断
- 设置持久化
- 断线重连、顶号、被踢提示
- Windows 安装包

### 3.2 需要 core 扩展后完成

- 本地摄像头/屏幕预览真实画面
- 摄像头设备枚举与指定设备启动
- 可选：call.invite/接听/挂断完整呼叫 UI

### 3.3 不在本计划内

- macOS/Linux/RK3576 生产客户端
- 多节点 SFU
- Simulcast
- 录制/GB28181
- E2EE

## 4. 面向生产的目标架构

```text
┌──────────────────────────────────────────────────────────┐
│ talk-desktop (Qt5 /MD)                                   │
│                                                          │
│ Presentation                                             │
│   MainWindow · PrejoinPage · RoomPage · Dialogs           │
│                                                          │
│ Presentation State                                       │
│   SessionStore · RoomViewModel · PrejoinViewModel         │
│                                                          │
│ Application Services                                     │
│   SessionController · DeviceService · SettingsService     │
│   DiagnosticsService · VideoStreamRegistry                │
│                                                          │
│ Core Integration                                         │
│   TalkCoreClient · CommandCorrelator · EventMapper        │
└──────────────────────────┬───────────────────────────────┘
                           │ C ABI only
┌──────────────────────────▼───────────────────────────────┐
│ talk_core.dll (/MT)                                      │
│ Signaling · Session · Media · P2P/SFU · Video frames      │
└──────────────────────────┬───────────────────────────────┘
                           │
                    WebRTC / Server
```

关键约束：

- UI target 只能包含 `talk/talk.h`，不能包含 WebRTC 或 `core/` 私有头文件
- `TalkCoreClient` 是 UI 中唯一允许直接调用 `talk_command()` 的模块
- `SessionController` 是页面唯一依赖的应用接口
- UI 页面不得直接解析协议 JSON
- 所有 QWidget 操作必须在 Qt UI 线程
- 视频帧回调必须从 WebRTC 媒体线程切回 UI 线程

## 5. 建议目录结构

```text
client-win/
  apps/
    talk-desktop/
      main.cpp
      app_bootstrap.*
  ui/
    desktop/
      api/
        talk_core_client.*
        core_command.*
        core_event.*
        event_mapper.*
      domain/
        session_state.*
        peer_state.*
        room_policy.*
        media_stream.*
        floor_state.*
        media_stats.*
      state/
        session_store.*
        stream_registry.*
      services/
        session_controller.*
        device_service.*
        settings_service.*
        diagnostics_service.*
        log_service.*
      pages/
        prejoin_page.*
        room_page.*
      viewmodels/
        prejoin_view_model.*
        room_view_model.*
      widgets/
      video/
        video_surface.*
        video_surface_factory.*
        image_video_surface.*
      theme/
      resources/
  tests/
    desktop/
  ui/qt/                 # 保留旧 demo，作为诊断和联调工具
  qt-ui-prototype/       # UI 视觉参考，不进入生产安装包
```

## 6. 模块设计

### 6.1 TalkCoreClient

职责：

- 创建和销毁 `talk_handle`
- 注册事件/视频回调
- 将 core 回调安全切到 Qt UI 线程
- 维护 generation，避免旧句柄事件污染新会话
- 对外提供类型化命令

建议命令封装：

```cpp
uint64_t Connect(const QString& url, const QString& token);
uint64_t JoinRoom(const QString& roomId, const QString& display,
                  const RoomPolicy& initialPolicy);
uint64_t LeaveRoom();
uint64_t SetMicMuted(bool muted);
uint64_t StartVideo(VideoSource source);
uint64_t StopVideo(VideoSource source);
uint64_t RequestFloor();
uint64_t ReleaseFloor();
uint64_t HostGrant(const QString& uid, bool allow);
uint64_t HostMuteAll(const QStringList& except = {});
uint64_t HostKick(const QString& uid);
uint64_t HostSetPolicy(const RoomPolicyPatch& patch);
uint64_t RefreshDevices();
uint64_t SetAudioSource(AudioSource source);
uint64_t SetVolume(double gain);
uint64_t RequestStats();
```

### 6.2 EventMapper / CommandCorrelator

当前 `result` 事件包含 `id` 和 `method`，可以关联命令：

```json
{"id":123,"method":"room.join","ok":true,"data":{...}}
```

生产层必须：

- 保存 `id -> command context`
- 对超时命令给出 UI 错误
- 命令失败时回滚乐观 UI 状态
- 区分业务错误、网络错误和媒体错误

### 6.3 SessionStore

单一状态源，至少包含：

- 连接状态：disconnected/connecting/ready/reconnecting
- 当前房间、角色、媒体模式
- 自身状态：uid/display/mic/camera/screen/volume/source
- 成员 Map：uid -> PeerState
- 房间策略 RoomPolicy
- FloorState：holder/queue/self/deadline
- 音量 levels、主讲人 uid
- 视频流注册表
- 最近错误和诊断数据

页面只订阅结构化状态变化信号。

### 6.4 RoomPolicy

生产 UI 需要展示和修改：

- `talk_mode`: free / ptt / moderated
- `max_speakers`
- `ptt_max_hold_sec`
- `ptt_queue`
- `media_mode`
- `p2p_allow_relay`
- `max_peers`
- `video.enabled`
- `video.max_publishers`

`host.setPolicy` 必须发送局部 patch，不能发送整套默认值。

### 6.5 VideoStreamRegistry

当前 demo 已经实现 mid 级视频管理，应抽成独立模块：

- key: mid
- uid
- source: camera / screen
- producer paused
- last frame timestamp
- surface pointer / frame consumer
- 5 秒无帧兜底回收
- `media.producer(paused)` 立即移除或禁用画面
- 恢复时等待新帧重新建视图

### 6.6 视频 Surface

现有 UI 原型使用 `QWidget + QImage`。生产首版建议：

- `ImageVideoSurface` 继承 QWidget，保存最新 QImage
- 媒体线程先拷贝 QImage，再用 queued connection 投递 UI 线程
- 按 33ms 合并刷新，避免每条帧都触发重绘
- 后续可替换为 D3D/OpenGL Surface

`VideoSurfaceFactory` 注入 `VideoTile`，避免 Tile 内部写死 mock Surface。

### 6.7 DeviceService

一期：

- 音频采集/播放设备枚举
- 采集源 mic/system
- 播放音量
- 设置持久化

需要 core 扩展：

- 摄像头设备枚举
- `video.start {source:"camera", device_id}` 指定设备
- 本地预览回调

### 6.8 DiagnosticsService

- 每 10 秒请求 `media.stats`
- 保留最近日志环形缓冲区
- 记录信令、媒体模式、视频 mid、设备、错误
- 诊断面板可导出文本
- 不记录 token、完整 SDP、密码

## 7. 线程与生命周期设计

```text
Qt UI thread
  SessionController / SessionStore / widgets
        ▲ queued signals
        │
talk core control thread
  event callback
        │
WebRTC media thread
  BGRA video callback
```

规则：

- event callback 不阻塞、不销毁句柄
- video callback 只拷贝帧，不做 QWidget 操作
- `talk_destroy()` 必须在所有 API 调用停止后、非回调线程执行
- 离房时停止视频回调并清空 StreamRegistry
- 重连时保留会话快照，但丢弃旧 generation 的帧和结果
- 所有帧队列有上限，超出时丢旧帧

## 8. 分阶段开发计划

### P0：分支与基线（0.5 天）

任务：

- 将当前 UI 原型未提交改动保存为基线提交
- 新建 `codex/feat/windows-production-client`
- 只读 fetch/检查 `origin/main@4a24f30`，记录为联调服务端版本
- **不 merge、不 cherry-pick、不修改其他分支**
- 构建 talk_core、talk-cli、talk-qt，跑 ctest
- 冻结 qt-ui-prototype 作为视觉参考

验收：

- 生产分支从客户端分支建立
- `client-win` 完整保留
- C API 和 demo 构建通过
- 现有测试通过
- 联调服务端版本已记录
### P1：生产 UI 骨架（1 天）

任务：

- 新增 `talk-desktop` target
- 建立 `ui/desktop` 目录和模块边界
- 移植主题、基础控件、MainWindow、PrejoinPage、RoomPage
- 加入 `TALK_BUILD_LEGACY_QT_DEMO` 和 `TALK_BUILD_DESKTOP` 选项
- 生产安装包只包含 talk-desktop

验收：

- talk-desktop 独立启动
- 新工程不包含业务 mock
- 旧 talk-qt 不再作为主发布程序

### P2：Core Bridge 与状态层（1.5 天）

任务：

- 实现 TalkCoreClient
- 实现 CommandCorrelator 和 EventMapper
- 实现 SessionStore
- 实现连接、joined、peerJoined/Left/Updated、result、error 映射
- 单元测试事件映射和命令结果

验收：

- 无 UI 情况下可以连接、入房、离房
- 成员列表来自真实事件
- 命令失败能回滚 UI

### P3：入房与会话生命周期（1.5 天）

任务：

- Prejoin 真实接入：服务地址、uid、房间、token、dev token、音频后端、模式、talk mode
- QSettings 持久化，不保存 token
- 连接/hello/join/leave 状态机
- 断线、重连、顶号、被踢提示
- 入房页错误提示

验收：

- 真实服务端可入房/离房
- 重连不重复入房
- 被踢不会自动重连
- 设置重启后保留

### P4：房间成员、对讲和主持人（2 天）

任务：

- 成员角色、静音、摄像头、屏幕共享状态映射
- 麦克风、摄像头、共享按钮真实命令
- PTT 按住/释放、排队、倒计时、revoked
- 主持人对讲策略面板
- 授予/收回、全员静音、移出
- 音量条、主讲人高亮
- speaking/levels 衰减策略

验收：

- 两人 PTT 排队和递补
- 主持人策略局部修改不重置其他字段
- 全员静音和被踢流程正确
- 音量与主讲人数据可见

### P5：真实视频与宫格（2.5 天）

任务：

- 按 mid 注册远端流
- camera/screen 双流并存
- 视频宫格、聚焦、共享布局接入真实流
- producer paused 立即更新
- 5 秒无帧回收
- 本地预览 core 回调扩展
- 摄像头设备枚举/选择 core 扩展

验收：

- 双人 P2P 摄像头/屏幕互看
- 第三人加入切 SFU 后画面续传
- 多路远端视频角标正确
- 本地和远端画面不串流

### P6：诊断、统计和错误处理（1.5 天）

任务：

- 10 秒 media.stats
- 状态页显示真实 RTT/码率/丢包/设备
- 日志落盘与诊断导出
- media.error/error 统一错误模型
- 设备切换失败回滚
- 视频启动失败按钮恢复

验收：

- 状态面板与 core 统计一致
- 诊断可导出
- 错误不会让按钮永久禁用

### P7：测试、打包与收口（1.5 天）

任务：

- Qt UI 单元测试
- 真实服务器双人/三人联调
- 六场景截图回归
- 125%/150% DPI
- 安装包 windeployqt + talk_core.dll + log-cpp.dll
- README、运行手册、故障排查
- 版本资源、日志路径、崩溃转储预留

验收：

- 安装包可在干净 Windows 机器运行
- 所有 P0–P6 门禁通过
- 真机音频/视频/PTT/host 验收完成

总预估：**12–14 个有效工作日**。

## 9. 测试矩阵

### 9.1 单元测试

- 命令 ID 关联
- result/error 映射
- 成员增删改和排序
- FloorState 倒计时、排队、revoked
- producer paused 状态收敛
- mid 流注册与回收
- 设置读写和 token 不落盘
- policy patch 序列化

### 9.2 集成测试

- connect + hello + join + leave
- 2 人 P2P
- 第 3 人加入后 P2P -> SFU
- SFU 多路视频
- PTT 抢麦/排队/超时
- host.grant/muteAll/kick
- host.setPolicy 局部更新
- 断线重连
- 被踢/顶号
- 设备切换和音频源切换

### 9.3 UI 回归

- 入房页
- 多人宫格
- 聚焦
- 1v1 画中画
- 屏幕共享
- 设备面板
- 状态面板
- 诊断抽屉
- 设置/离开弹窗
- 1100×720、125%、150%

### 9.4 稳定性

- 30 分钟双人通话
- 2 小时多人视频
- 断网 10 秒恢复
- 连续开关摄像头/共享 100 次
- 连续设备切换
- 长时间音频电平推送

## 10. 发布与打包

生产安装包需要包含：

- `talk-desktop.exe`
- `talk_core.dll`
- `log-cpp.dll`
- fmt 和 Qt 运行时
- Qt platform/imageformats 插件
- 版本信息和图标

可选包含：

- `talk-cli.exe`，仅作为现场诊断工具，不放在主安装目录或由单独诊断包提供

安装器：

- 更新现有 `installer/talk.iss` 应用程序名和 exe
- 更新 `scripts/package.ps1`
- 安装前结束 `talk-desktop.exe`
- 卸载清理 QSettings
- 日志目录使用 `%LOCALAPPDATA%`，不写安装目录

## 11. 风险与决策点

### 11.1 已识别风险

| 风险 | 处理 |
|---|---|
| main 删除 client-win | `main` 只读；生产分支保留 client-win，CI 校验关键文件存在 |
| `/MT` core 与 `/MD` UI CRT 冲突 | 只通过 C ABI 链接，不暴露 C++ 对象 |
| 视频回调线程 | 只传 QImage，queued connection 回 UI 线程 |
| 本地预览未实现 | 一期补 C API 本地帧回调 |
| 摄像头设备选择未实现 | 补 video device API，否则只能默认摄像头 |
| call 流程 core 未实现 | 不在首版默认范围，作为后续独立阶段 |
| stats 格式可能扩展 | EventMapper 保持容错，未知字段忽略 |
| PTT 操作复杂 | 用状态机模块和单元测试隔离 |
| 多路视频缩放 | Surface 只保留最新帧，按 30fps 合并 |

### 11.2 已确认的边界

1. 首版 1v1 使用“直接加入房间”，不包含 call.invite 呼叫弹窗。
2. 本地预览回调与摄像头设备选择列为 P5 必做项。
3. 主持人策略面板作为一期必做项。
4. `talk-cli.exe` 单独作为诊断包，不放入主安装目录。

## 12. Git 与交付方式

执行规则：

```text
1. 生产开发只在 codex/feat/windows-production-client
2. main 和其他分支只读
3. 需要服务端能力时，记录 origin/main 提交号并请服务端分支独立发布
4. 需要协议文档时，手工同步文档内容，不合并其他分支
5. 按 P0–P7 分批实现
6. 每个阶段构建 + 测试 + 截图
7. 每阶段暂停等待确认
```

禁止修改、合并或消费其他分支作为可写来源；如发现其他分支逻辑问题，只在评审意见和本文档中记录。

## 13. 完成定义

生产客户端达到以下条件才算完成：

- UI 仅通过 C API 访问核心
- 真实连接、入房、离房、成员、媒体控制可用
- 双人 P2P 和多人 SFU 均可用
- 摄像头和屏幕共享双流可区分
- PTT/主持管控/音量/主讲人可用
- 设备、配置、诊断可用
- 断线恢复和被踢流程正确
- 安装包可在干净 Windows 环境运行
- 单元、集成和 UI 回归通过
- 文档和故障排查手册齐全

### P0 执行结果（2026-10-26）

- 生产分支：`codex/feat/windows-production-client`
- 分支基线提交：`8b705e3`
- 当前生产分支 HEAD：`8b705e3`
- 服务端只读参考版本：`origin/main@4a24f30`
- `main` 本地版本保持 `566cba9`，未修改
- `feat/windows-client` 保持 `df22708`，未修改

构建：

- `cmake --build --preset windows-x64-release --parallel`：通过
- `talk_core.dll`：通过
- `talk-cli.exe`：通过
- `talk-qt.exe`：通过
- UI 原型 `rtc_talk_qt_ui.exe`：通过

测试：

- `api_test`：通过
- `protocol_test`：通过
- `signaling_test`：通过
- `video_codec_test`：通过
- `camera_probe`：通过
- 合计 5/5 通过

说明：

- P0 不修改 `main` 或其他分支
- P0 不合并服务端分支
- 计划文档修改仅保存在当前生产分支工作区，尚未提交
