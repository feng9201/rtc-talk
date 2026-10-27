# rtc-talk-win 软件架构包

> 状态：待评审，未提交。
> 日期：2026-10-26
> 需求基线：`docs/requirements.md`
> 本文替代此前的单页 `docs/architecture.md` 草稿。

## 1. 文档目标

本架构包用于指导 `rtc-talk-win` 从零实现生产级 Windows RTC 客户端。
它不是目录建议，也不是概念图，而是后续接口、CMake target、模块代码、
测试和验收的直接依据。

评审通过后，以下内容视为实现基线：

- 进程/DLL/线程边界。
- 模块职责和禁止依赖。
- 领域模型、命令、事件和错误模型。
- 信令、会话和媒体状态机。
- UI、ViewModel 和视频渲染边界。
- C ABI 版本化约定。
- 工程目录、CMake target 图和第三方依赖策略。
- 测试、诊断、安全和发布要求。

## 2. 文档索引

| 文档 | 内容 |
|---|---|
| [01-system-context.md](01-system-context.md) | 系统上下文、进程/DLL 边界、部署拓扑 |
| [02-module-architecture.md](02-module-architecture.md) | 模块目录、职责、公开接口、依赖图 |
| [03-domain-model.md](03-domain-model.md) | 实体、值对象、命令、事件、错误、不变量 |
| [04-engine-and-session.md](04-engine-and-session.md) | 引擎运行时、信令、RPC、会话和房间状态机 |
| [05-media-architecture.md](05-media-architecture.md) | 音频、摄像头、屏幕、WebRTC、预览/发布/订阅管线 |
| [06-ui-architecture.md](06-ui-architecture.md) | 应用服务、ViewModel、Qt 页面、模型、视频 Surface |
| [07-concurrency-lifecycle.md](07-concurrency-lifecycle.md) | 线程所有权、回调规则、关闭顺序、generation |
| [08-c-abi.md](08-c-abi.md) | 版本化 C ABI、命令/事件协议、帧回调、兼容策略 |
| [09-runtime-flows.md](09-runtime-flows.md) | 启动、连接、入房、预览、发布、重连、退出时序 |
| [10-build-and-dependencies.md](10-build-and-dependencies.md) | CMake target 图、CRT、libwebrtc、vcpkg、目录和生成物 |
| [11-testing-and-observability.md](11-testing-and-observability.md) | 单元/组件/集成/UI/稳定性测试，日志和诊断 |
| [12-security-and-release.md](12-security-and-release.md) | Token、WSS、脱敏、安装包、版本和发布门禁 |
| [13-architecture-decisions.md](13-architecture-decisions.md) | 已定决策、待确认决策和 ADR 清单 |
| [14-review-checklist.md](14-review-checklist.md) | 架构评审项、编码前放行条件 |

## 3. 核心架构摘要

```text
┌───────────────────────────────────────────────────────────────┐
│ rtc-talk.exe                                     /MD          │
│                                                               │
│ Presentation: Qt Widgets / ViewModel / Video Surface          │
│      │                                                        │
│ Application: Session / Device / Diagnostics / Settings        │
│      │                                                        │
│ Client Gateway: typed DTO <- C ABI events, C ABI -> commands  │
└──────┼────────────────────────────────────────────────────────┘
       │ rtcwin_client_api.h
┌──────▼────────────────────────────────────────────────────────┐
│ rtcwin_engine.dll                                /MT          │
│                                                               │
│ EngineRuntime / CommandRouter / EventBus                      │
│ SessionEngine / Room / Floor / Host / MediaMode               │
│ MediaCoordinator / Preview / Publish / Subscribe              │
│                                                               │
│ ┌────────────┐ ┌────────────┐ ┌────────────┐ ┌─────────────┐ │
│ │ signaling  │ │ camera_win │ │ audio_win  │ │ screen_win  │ │
│ └────────────┘ └────────────┘ └────────────┘ └─────────────┘ │
│ ┌───────────────────────────────────────────────────────────┐ │
│ │ webrtc_media: PeerConnection / SDP / ICE / tracks / stats │ │
│ └───────────────────────────────────────────────────────────┘ │
└───────────────────────────────────────────────────────────────┘
```

## 4. 架构原则

1. **领域优先**：先定义业务状态和端口，再选择 Qt、WebRTC 和 Windows API。
2. **端口/适配器**：业务不依赖 WebRTC、Media Foundation、WASAPI、WGC、Qt QWidget。
3. **单一所有权**：每个运行时资源只有一个明确 owner 和一个关闭入口。
4. **状态机显式化**：会话、预览、发布、P2P/SFU 切换不得用零散 bool 表达。
5. **跨线程传值**：UI 与引擎之间只传拷贝后的强类型快照和帧视图。
6. **失败优先**：每个命令必须定义超时、取消、失败回滚和资源清理。
7. **可测试性**：核心模块均支持 Fake transport、Fake capture 和 Fake clock。
8. **渐进替换**：首版 QImage Surface 可用，但不绑定业务；后续可换 D3D/OpenGL。
9. **不复制 demo 结构**：demo 只提供行为样本，不能作为生产类层次来源。
10. **基线可追溯**：每个能力、测试和提交映射到需求编号。

## 5. 需要评审确认的决策

以下决策影响工程创建，必须确认后才进入实现：

| ID | 决策 | 当前建议 |
|---|---|---|
| D-01 | 仓库形态 | 独立仓库 `rtc-talk-win`，新分支同名 |
| D-02 | Qt 版本 | Qt 5.15.2 |
| D-03 | 正式服务 WSS | 必须支持并校验证书 |
| D-04 | Token 记忆 | 默认不持久化；记住时用 Windows Credential Manager |
| D-05 | 测试框架 | GoogleTest + QtTest |
| D-06 | 屏幕共享范围 | 一期仅主屏 |
| D-07 | 诊断工具 | 独立诊断包，不进入主安装目录 |
| D-08 | 视频渲染首版 | QImage Surface |
| D-09 | C ABI 命令/事件编码 | method + JSON 仅限 C ABI 边界 |
| D-10 | 第三方依赖引入 | vcpkg manifest 锁定 |

## 6. 编码前放行条件

- 需求文档确认。
- 本架构包评审通过。
- D-01 至 D-10 有明确结论。
- `production-plan.md` 按最终架构重写并确认。
- W0 只创建工程骨架，不接入 demo 代码。
- 依赖检查、CI 和测试框架先通过。
