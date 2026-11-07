# UI 架构及交互

## 1. 分层

```text
Qt Widgets (Views) ← render immutable ViewState
        │ 用户意图
        ▼
Presenter / CommandFacade → Application ports → Engine
        ▲                                       │ typed events
        └──── UiStore（仅 Qt 主线程更新）← queued bridge

WebRTC scheduled frame → latest mailbox → VideoSurfaceHost → Wayland
                （帧完全不经过 UiStore / JSON / Qt queued 每帧事件）
```

Widgets 不解析原始 WS JSON，不写 SDP，不直接操作 ADM/MPP。Presenter 把“希望开麦”
与“已获准发言”分开；UiStore 依据服务端和媒体状态渲染，不以按钮点击乐观宣布成功。
ViewState 含 generation、busy/error、connection、room、permissions、peers、video tiles 和设备状态。
只有拥有 UI 线程亲和性的对象访问 QWidget/QScreen；跨线程桥绑定 QObject 生命周期并检查 generation。

## 2. 页面和组件

| 页面 | 主要组件 | 状态/限制 |
|---|---|---|
| 启动/预检 | 版本、配置检查、音频/MPP/Wayland 能力 | 不自动开麦；失败可看诊断，不直接崩溃 |
| 连接/认证 | 服务地址、业务凭证提供者状态、连接按钮 | 正式版不展示 dev-token，不保存明文凭证 |
| 入房 | 房间号、显示名、音频设备、入房按钮 | 房间号校验，默认静音，无摄像头预览/设置 |
| 房间 | 视频主区域、成员栏、工具栏、连接状态 | M1 单主画面，screen/camera 来源标识 |
| 设备/设置 | 输入/输出、音量、选屏、日志级别 | 设备切换先关麦，失败不偷偷回退到别的麦克风 |
| 诊断 | 请求错误、codec、loss/RTT、解码/渲染状态 | 可导出脱敏快照，无 JWT/SDP/SSH 密码 |
| 呼叫/发言管理 | 呼入对话框、PTT、队列/主持操作 | 在对应里程碑实现，不在 M1 假装可用 |

房间工具栏最少包括：静音、音量、选视频来源、离房；没有摄像头/共享按钮。
一个成员可有 camera 和 screen 两个流，不能用单一 video.on 覆盖真实流状态。
暂停视频显示暂停占位；结束/离房清除末帧；等待关键帧、解码失败、无显示器分别展示。
UI 运行状态、音频已连通、收到视频、已解码、已提交/呈现是不同指标，不统一画绿色“正常”。

## 3. 显示合成与布局

VideoSurfaceHost 是透明占位 QWidget，只管理矩形/可见性/屏幕锚点。
RK 的实际视频由独立 Wayland subsurface 承载。顶层透明区域和 Weston 层级必须真板验证。
从 sbdc 当前 render 实现保留：ScreenName 锚点、异步创建、共享几何校准、断连接口、
热插拔静默和帧 lease；不把 NVR AI overlay/业务通道号带进来。
支持等比例 fit + 黑边；分辨率变化按实际 crop/stride 处理，不拉伸或忽略旋转。
M1 单屏单主画面；多分屏在资源准入后扩展，布局变化不重建整个 WebRTC 会话。
wayland 禁止 UI 线程同步等待显示 configure，防止出现“SSH正常但应用卡死”的情况。

## 4. 关键交互

- 开麦按钮：先提交请求并 pending，成功且所有 gate 条件满足才显示“发言中”。失败显示具体原因。
- 关麦按钮/全局安全停止：立即关闭本地 gate，再更新远端；不能因请求排队继续漏音。
- PTT：press 发 request，只有 granted 后开；release/focusOut/窗口隐藏/触控取消/断线都关麦。
- 连接恢复：显示 Recovering 和倒计时，禁用不安全操作；禁止反复弹窗或保存开麦状态自动复播。
- 入房/离房：重复点击幂等或明确 Busy；关闭窗口走异步停止，超时有有界兜底。
- 自动重连不恢复已被 kicked/replaced 的会话，不绕过房间权限。
- 键盘焦点、触摸热区、对比度和错误文案统一；日志详情与用户提示分离。

## 5. UI 测试

Presenter/Store 用假 Application ports 验证状态，不伪造硬解成功。
Qt offscreen 测交互和关闭，真实 Wayland 才能验收视频可见、透明/层级、选屏和 resize。
关键路径：pending 开麦时离房、被撤权、旧代事件迟到、设备移除、关闭时仍有帧、
paused 与 0fps screen、host/member 切换、翻转屏幕/窗口隐藏恢复。
