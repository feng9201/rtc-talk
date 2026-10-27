# 02 模块架构与代码组织

## 1. 仓库目录

```text
rtc-talk-win/
  CMakeLists.txt
  CMakePresets.json
  vcpkg.json
  vcpkg-configuration.json
  cmake/
    dependencies.cmake
    webrtc.cmake
    warnings.cmake
    architecture-check.cmake
    packaging.cmake

  apps/
    rtc-talk/
      main.cpp
      bootstrap/
        app_bootstrap.h
        app_bootstrap.cpp
        dependency_container.h
        dependency_container.cpp
        single_instance_guard.h
        single_instance_guard.cpp
      diagnostics/
        crash_handler.h
        crash_handler.cpp

  libs/
    domain/
      CMakeLists.txt
      include/rtcwin/domain/
        types.h
        session_state.h
        room_state.h
        media_state.h
        device_state.h
        diagnostics_state.h
        commands.h
        events.h
        errors.h
        ports.h
        invariants.h
      src/
        session_reducer.cpp
        room_reducer.cpp
        media_reducer.cpp

    protocol/
      CMakeLists.txt
      include/rtcwin/protocol/
        envelope.h
        methods.h
        codec.h
        messages.h
        validation.h
      src/
        envelope.cpp
        codec.cpp
        validation.cpp
        message_mapping.cpp

    application/
      CMakeLists.txt
      include/rtcwin/application/
        session_coordinator.h
        prejoin_coordinator.h
        room_coordinator.h
        device_coordinator.h
        diagnostics_coordinator.h
        settings_service.h
        credential_service.h
        state_store.h
        view_models.h
      src/
        session_coordinator.cpp
        prejoin_coordinator.cpp
        room_coordinator.cpp
        device_coordinator.cpp
        diagnostics_coordinator.cpp
        settings_service.cpp
        credential_service.cpp
        state_store.cpp

    client_gateway/
      CMakeLists.txt
      include/rtcwin/client_gateway/
        engine_client.h
        c_abi_engine_client.h
        event_decoder.h
        frame_bridge.h
        command_codec.h
      src/
        c_abi_engine_client.cpp
        event_decoder.cpp
        frame_bridge.cpp
        command_codec.cpp

    engine/
      CMakeLists.txt
      include/rtcwin/engine/
        engine.h
        engine_runtime.h
        command_router.h
        event_bus.h
        correlation.h
        generation.h
      src/
        engine.cpp
        engine_runtime.cpp
        command_router.cpp
        event_bus.cpp
        correlation.cpp
      session/
        session_lifecycle.h
        session_lifecycle.cpp
        room_controller.h
        room_controller.cpp
        floor_controller.h
        floor_controller.cpp
        host_controller.h
        host_controller.cpp
        media_mode_coordinator.h
        media_mode_coordinator.cpp
      media/
        media_coordinator.h
        media_coordinator.cpp
        preview_session.h
        preview_session.cpp
        publish_pipeline.h
        publish_pipeline.cpp
        remote_video_pipeline.h
        remote_video_pipeline.cpp
        video_stream_registry.h
        video_stream_registry.cpp
        device_coordinator.h
        device_coordinator.cpp
      signaling/
        rpc_client.h
        rpc_client.cpp
        reconnect_policy.h
        reconnect_policy.cpp

    platform_win/
      CMakeLists.txt
      include/rtcwin/platform_win/
        clock.h
        paths.h
        time.h
        utf.h
        error_mapping.h
        credential_store.h
      src/
        clock.cpp
        paths.cpp
        utf.cpp
        error_mapping.cpp
        credential_store.cpp

    camera_win/
      CMakeLists.txt
      include/rtcwin/camera_win/
        camera_service.h
        media_foundation_camera_service.h
        camera_capture_session.h
      src/
        media_foundation_camera_service.cpp
        camera_capture_session.cpp
        media_foundation_device_enumerator.cpp
        media_foundation_capture_reader.cpp

    audio_win/
      CMakeLists.txt
      include/rtcwin/audio_win/
        audio_service.h
        wasapi_audio_service.h
        audio_device_monitor.h
      src/
        wasapi_audio_service.cpp
        audio_device_monitor.cpp

    screen_win/
      CMakeLists.txt
      include/rtcwin/screen_win/
        screen_service.h
        desktop_capture_service.h
      src/
        desktop_capture_service.cpp
        wgc_capture_session.cpp
        dxgi_capture_session.cpp

    webrtc_media/
      CMakeLists.txt
      include/rtcwin/webrtc_media/
        peer_connection_factory.h
        webrtc_peer_connection.h
        sfu_media_transport.h
        p2p_media_transport.h
        video_frame_bridge.h
        rtc_ice_config.h
        rtc_stats_adapter.h
      src/
        webrtc_peer_connection_factory.cpp
        webrtc_peer_connection.cpp
        sfu_media_transport.cpp
        p2p_media_transport.cpp
        video_frame_bridge.cpp
        rtc_ice_config.cpp
        rtc_stats_adapter.cpp

    c_api/
      CMakeLists.txt
      include/rtcwin/client_api.h
      src/
        client_api.cpp
        callback_bridge.cpp
        abi_validation.cpp

    muxplayer_adapter/
      CMakeLists.txt
      include/rtcwin/muxplayer_adapter/
        mux_player_environment.h
        mux_player_session.h
        mux_player_pool.h
        mux_video_surface.h
        raw_frame_injector.h
        mux_capabilities.h
        mux_playback_policy.h
      src/
        mux_player_environment.cpp
        mux_player_session.cpp
        mux_player_pool.cpp
        mux_video_surface.cpp
        raw_frame_injector.cpp
        mux_capabilities.cpp
        mux_playback_policy.cpp

    ui/
      CMakeLists.txt
      shell/
        app_window.h
        app_window.cpp
        window_controls.h
        window_controls.cpp
        toast_host.h
        toast_host.cpp
      pages/
        prejoin_page.h
        prejoin_page.cpp
        room_page.h
        room_page.cpp
      components/
        member_list_model.h
        member_list_model.cpp
        member_row.h
        member_row.cpp
        control_bar.h
        control_bar.cpp
        device_panel.h
        device_panel.cpp
        status_panel.h
        status_panel.cpp
        policy_dialog.h
        policy_dialog.cpp
        diagnostics_drawer.h
        diagnostics_drawer.cpp
      video/
        video_surface.h
        image_video_surface.h
        image_video_surface.cpp
        video_surface_factory.h
        video_tile.h
        video_tile.cpp
        video_layout_engine.h
        video_layout_engine.cpp
        video_grid.h
        video_grid.cpp
      theme/
        theme.h
        theme.cpp
        theme.qss

  tests/
    unit/
      domain/
      protocol/
      application/
      engine/
      video_layout/
    component/
      camera/
      audio/
      screen/
      preview/
      media_coordinator/
      client_gateway/
    integration/
      signaling/
      session/
      media_loopback/
    ui/
      prejoin/
      room/
      video/
      dpi/
    smoke/
      p2p/
      sfu/
      reconnect/
      host/
    fixtures/
      fake_signaling_transport.h
      fake_engine.h
      fake_camera_service.h
      fake_audio_service.h
      fake_screen_service.h
      fake_clock.h
      fake_frame_source.h

  tools/
    rtcwin-probe/
    rtcwin-cli/

  third_party/
    muxplayer/
      VERSION.md
      PATCHES.md
      patches/
      source/

  packaging/
    windows/
      installer.iss
      runtime-manifest.json
      license-list.md

  scripts/
    configure.ps1
    build.ps1
    test.ps1
    architecture-check.ps1
    package.ps1

  docs/
    requirements.md
    architecture/
    integration-baseline.md
    coding-standard.md
    runbook.md
    troubleshooting.md

  plan/
    production-plan.md
```

## 2. 模块总览

| 模块 | 层 | 运行库 | 主要职责 | 不允许依赖 |
|---|---|---|---|---|
| `domain` | Domain | `/MD` | 类型、事件、命令、错误、端口 | Qt、WebRTC、Windows、JSON |
| `protocol` | Infrastructure/协议 | `/MT` | talk.v1.json 编解码和校验 | Qt UI |
| `application` | Application | `/MD` | 状态协调、设置、设备意图、诊断 | QWidget、WebRTC |
| `client_gateway` | Infrastructure | `/MD` | C ABI 调用、事件转 DTO、帧桥 | WebRTC、QWidget |
| `engine` | Engine | `/MT` | 会话、房间、发言权、媒体编排 | QWidget、QSettings |
| `camera_win` | Adapter | `/MT` | MF 摄像头枚举和采集 | Session、UI |
| `audio_win` | Adapter | `/MT` | WASAPI 设备和音频源 | Session、UI |
| `screen_win` | Adapter | `/MT` | WGC/DXGI 屏幕采集 | Session、UI |
| `webrtc_media` | Adapter | `/MT` | PeerConnection、SDP、ICE、发送/接收 | UI、Settings |
| `platform_win` | Infrastructure | `/MT` | 时钟、路径、UTF、Credential、错误映射 | UI |
| `c_api` | Boundary | `/MT` | ABI 导出、回调桥、参数校验 | Qt |
| `muxplayer_adapter` | UI infrastructure | `/MD` | muxPlayer 生命周期、帧注入、GPU、Surface、播放池 | UI 页面、Engine、WebRTC |
| `ui` | Presentation | `/MD` | Shell、页面、组件、Surface | WebRTC、协议 JSON |

## 3. Domain 模块

### 3.1 职责边界

`domain` 是业务语言和状态所有权的唯一来源，负责：

- 会话、房间、成员、发言权、媒体模式、设备错误。
- 状态不变量。
- 命令和事件类型。
- 应用层与基础设施之间的端口。

### 3.2 关键不变量

| 不变量 | 说明 |
|---|---|
| INV-001 | `RoomJoined` 才能产生与房间相关的命令。 |
| INV-002 | 同一 `mid` 只能属于一个远端流。 |
| INV-003 | 同一 uid 的同一采集源最多一个有效 producer。 |
| INV-004 | `producer_paused=true` 时不得显示该流最后一帧。 |
| INV-005 | 发言权不是本端状态，必须由服务端事件授权。 |
| INV-006 | 被踢后不能自动重连。 |
| INV-007 | 主动离房后不能恢复房间。 |
| INV-008 | 视频帧的 `mid` 必须匹配已注册流才进入 UI。 |
| INV-009 | 命令结果只能影响同一 generation。 |
| INV-010 | 预览停止不得改变正式摄像头发布状态。 |

### 3.3 reducer

每个状态由纯函数 reducer 维护：

```cpp
SessionState Reduce(const SessionState&, const SessionEvent&);
RoomState    Reduce(const RoomState&, const RoomEvent&);
MediaState   Reduce(const MediaState&, const MediaEvent&);
```

要求：

- 不读系统时间、网络、文件。
- 不产生副作用。
- 可重复处理重复事件。
- 对乱序事件保留版本或 last-updated 判定。

## 4. Protocol 模块

### 4.1 结构

```text
envelope.h    v/type/id/method/data/error
methods.h     所有已知 method 常量
messages.h    强类型请求/响应/通知
validation.h  字段、长度、枚举、ID 校验
codec.h       JSON <-> strong typed messages
```

### 4.2 规则

- 原始 JSON 对象禁止越过 `protocol` 模块。
- 未知字段忽略。
- 未知 method 映射到 `UnknownEvent`，不得丢弃整个连接。
- 超大消息、无效 UTF-8、错误类型直接拒绝并上报协议错误。
- 服务端请求和客户端请求都支持，但保留 method 白名单和方向约束。

## 5. Application 模块

### 5.1 Coordinators

```text
SessionCoordinator
  connect/join/leave/reconnect
  command intent -> IEngineClient
  engine events -> state store

PrejoinCoordinator
  form state
  device enumeration
  camera preview
  validation before join

RoomCoordinator
  member/floor/policy/video view state
  control commands

DeviceCoordinator
  audio/camera/screen devices
  selected device state
  hotplug watcher

DiagnosticsCoordinator
  stats poll
  diagnostic export
```

### 5.2 状态存储

```text
StateStore
  PrejoinState
  SessionState
  RoomState
  DeviceState
  DiagnosticsState
```

状态存储只接受应用层的 typed action，不直接接受 C ABI JSON。

### 5.3 页面输入

页面通过 coordinator 发送 intent：

```text
JoinRequested
LeaveRequested
MicToggleRequested
CameraToggleRequested
ShareToggleRequested
DeafenToggleRequested
PreviewToggleRequested
MemberSelected
FloorPressed
FloorReleased
HostActionRequested
```

coordinator 负责校验当前状态，再生成引擎命令。

## 6. Engine 模块

### 6.1 EngineRuntime

拥有：

- 控制线程。
- asio io_context。
- 命令队列。
- 定时器。
- generation。
- 事件输出桥。
- 媒体线程协调器。

### 6.2 子模块

```text
CommandRouter
  -> SessionCommands
  -> DeviceCommands
  -> MediaCommands
  -> FloorCommands
  -> HostCommands
  -> DiagnosticCommands

SessionLifecycle
  -> Signaling/RPC
  -> RoomController
  -> ReconnectPolicy

MediaCoordinator
  -> CameraService
  -> AudioService
  -> ScreenService
  -> PeerConnectionFactory
  -> PreviewSession
  -> PublishPipeline
  -> RemoteVideoPipeline
```

任何控制器不得直接操作 C ABI 回调，只通过 `EventBus` 发出领域事件。

## 7. Windows Adapter 模块

### 7.1 camera_win

文件职责：

| 文件 | 职责 |
|---|---|
| `media_foundation_device_enumerator` | 枚举、能力、权限状态 |
| `media_foundation_capture_reader` | Source Reader 生命周期 |
| `camera_capture_session` | 采集线程、NV12/I420/BGRA、帧分发 |
| `media_foundation_camera_service` | 对外服务、设备选择、热插拔 |

### 7.2 audio_win

| 文件 | 职责 |
|---|---|
| `wasapi_audio_service` | ADM 设备、播放/录音选择、音量 |
| `audio_device_monitor` | 设备变化通知 |

### 7.3 screen_win

| 文件 | 职责 |
|---|---|
| `desktop_capture_service` | 枚举和选择主屏 |
| `wgc_capture_session` | WGC 采集 |
| `dxgi_capture_session` | WGC 不可用时的 DXGI fallback |

## 8. WebRTC Adapter

WebRTC adapter 只负责媒体传输：

```text
PeerConnectionFactory
  CreateSendConnection()
  CreateReceiveConnection()
  CreateP2pConnection()

WebRtcPeerConnection
  CreateOffer/SetAnswer/SetRemoteOffer/CreateAnswer
  Add/RemoveTrack
  SetSenderActive
  AddIceCandidate
  Sink/Stats callbacks

SfuMediaTransport
  Publish/Subscribe SDP handling

P2pMediaTransport
  Offer/Answer/Candidate
```

它不负责：

- 房间成员。
- 发言权。
- 重连策略。
- UI 错误文案。
- 设备枚举 UI。

## 9. UI 模块

### 9.1 Shell

- 无边框窗口。
- 最小化、最大化、关闭。
- 页面栈。
- ToastHost。
- 全局错误/离线遮罩。

### 9.2 Pages

`PrejoinPage` 只绑定 `PrejoinState`，`RoomPage` 只绑定 `RoomState`。

### 9.3 Components

- `MemberListModel`：成员数据变化通过 model reset/rows changed。
- `DevicePanel`：设备列表、选择、刷新。
- `StatusPanel`：连接、媒体、统计。
- `PolicyDialog`：主持人策略 patch。
- `DiagnosticsDrawer`：日志和统计导出。

### 9.4 Video

```text
MuxVideoSurface
  muxplay::GLPlayWidget
  muxplay::D3DRenderWidget

MuxPlayerPool
  MuxPlayerSession per active mid
  shared MuxRenderEnvironment
  RawFrameInjector

VideoGrid
  VideoLayoutEngine
  VideoTile
  MuxVideoSurface
```

强制约束：

- 所有远端 RTC 视频、本地摄像头预览和共享画面统一经 muxPlayer 渲染。
- 页面不得直接创建 `MultiplexPlayer` 或 `GLPlayWidget`。
- UI 只能依赖 `rtcwin_muxplayer_adapter` 暴露的 `MuxVideoSurface`。
- muxPlayer 运行在 UI 进程，通过 `/MD` 构建，不进入 `/MT` engine。

## 10. 模块依赖检查

在 CI 和本地执行：

```powershell
scripts/architecture-check.ps1
```

检查项：

- domain 不允许 Qt/WebRTC/Windows/JSON include。
- UI 不允许 WebRTC/asio/protocol 私有头文件。
- application 不允许 QWidget/WebRTC。
- 只有 `client_gateway` 可以 include `rtcwin/client_api.h` 并调用 `rtcwin_*`。
- 播放/采集 adapter 不允许 include UI。
- CMake target 依赖只按本文目标图连接。

## 11. 模块完成标准

每个模块至少包含：

- public headers。
- ownership 注释。
- 错误映射。
- 单元测试。
- Fake 实现（如果它是端口）。
- 日志字段。
- 关闭和失败测试。
- 架构检查通过。
