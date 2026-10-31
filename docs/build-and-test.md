# Build and Test

## Toolchain prerequisites

- Windows 10/11 x64
- Visual Studio 2022 with Desktop development with C++
- CMake 3.24 or newer
- Qt 5.15.2 MSVC 2019 x64
- vcpkg

Qt, Visual Studio, and vcpkg are development-environment tools. Product source
dependencies are stored in this repository.

## Bundled dependencies

- `third_party/muxplayer`: complete user-wrapped muxPlayer source tree
- `third_party/mtAudio`
- `third_party/mtAudioPlayer`
- `third_party/tmcore`
- `third_party/flvparser`

`log-cpp[shared]@1.0.0#4` is resolved from the private vcpkg registry configured
by `vcpkg-configuration.json`. Engine consumes it through
`rtcwin_log_adapter.dll`, which exposes only a C ABI so `/MT` WebRTC code never
crosses the `/MD` log-cpp CRT boundary.

The S1 build currently enables the raw-I420 software OpenGL renderer only. Full
muxPlayer WGL/D3D11 zero-copy sources remain in-tree and will be enabled in a
later stage with their package dependencies.

## Environment

```powershell
$env:VCPKG_ROOT = 'D:\vcpkg'
$env:QTDIR = 'C:\Qt\5.15.2\msvc2019_64'
$env:WEBRTC_SDK_DIR = 'D:\google\webrtc_sdk_hevc'
```

`CMakePresets.json` resolves Qt and vcpkg through these environment variables.
No product source or CMake file references another workspace path.

## Configure, build, and test

```powershell
cmake --preset windows-x64
cmake --build --preset windows-x64-release --parallel
ctest --test-dir build/windows-x64 -C Release --output-on-failure
```

## Run

```powershell
$env:PATH = "$env:QTDIR\bin;" + $env:PATH
.\build\windows-x64\bin\Release\rtc-talk.exe
```

## Portable ZIP

```powershell
pwsh -File .\scripts\package.ps1
```

The package contains the application, `rtcwin_engine.dll`, Qt runtime,
VC++ runtime, FFmpeg/fmt runtime dependencies, SHA256 manifest, usage notes,
and a diagnostic launcher. Never share only `rtc-talk.exe`; the executable and
Engine DLL are one compatibility pair.

## S1 media boundaries

- Current renderer: CPU I420 -> muxPlayer software OpenGL.
- S1 established the local-preview foundation; later S3/S4 work added WebRTC
  audio and SFU camera publication.
- Local preview does not decode a loopback H.264 stream.
- WGL/D3D11 zero-copy remains the required follow-up rendering backend.
## S2 signaling validation

The current development environment uses:

- Signaling: `ws://47.117.159.193:8080/talk/v1/ws`
- Dev token: `GET /dev/token?uid=<uid>&rooms=<room>`

Direct signaling smoke:

```powershell
$env:RTCWIN_SIGNALING_TOKEN = "<dev-token>"
.\build\windows-x64\bin\Release\rtcwin_signaling_smoke.exe `
  --url ws://47.117.159.193:8080/talk/v1/ws `
  --uid rtcwin-smoke `
  --room rtcwin-smoke
```

Full Qt UI auto-join diagnostics:

```powershell
.\build\windows-x64\bin\Release\rtc-talk.exe `
  --auto-join-url ws://47.117.159.193:8080/talk/v1/ws `
  --auto-join-room rtcwin-ui-smoke `
  --auto-join-uid rtcwin-ui-smoke `
  --auto-join-dev-token
```

The UI path fetches `/dev/token`, executes `session.hello`, `room.join`, and
updates the room snapshot and member list. `room.leave` returns to the prejoin
page.
## S3 libwebrtc/WASAPI baseline

libwebrtc is an external SDK resolved through `WEBRTC_SDK_DIR`; its source and
2+ GiB binary tree are not copied into Git. The project owns the media adapter
module under `libs/webrtc_media`.

Audio device probe:

```powershell
.\build\windows-x64\bin\Release\rtcwin_audio_devices_probe.exe
```

The probe initializes libwebrtc's Windows Core Audio ADM and prints recording and
playout devices. The same devices are exposed through the Engine `audio.devices`
command and P5 prejoin page selectors.

## B3 session state machine

`SessionStateMachine` is covered by a standalone test target:

```powershell
.\build\windows-x64\bin\Release\rtcwin_session_state_tests.exe
```

The full CTest set is currently 11 tests:

- `rtcwin_tests`
- `rtcwin_session_state_tests`
- `rtcwin_video_scene_tests`
- `rtcwin_frame_health_tests`
- `rtcwin_floor_host_controller_tests`
- `rtcwin_reconnect_controller_tests`
- `rtcwin_video_surface_pool_tests`
- `rtcwin_video_grid_stress_tests`
- `rtcwin_signaling_tests`
- `rtcwin_log_adapter_smoke`
- `rtcwin_media_session_smoke`

All signaling and media callbacks that can outlive a connection must carry the
session generation and be discarded when the generation changes.

## S4.1 camera SFU smoke

Media/session probe:

```powershell
.\build\windows-x64\bin\Release\rtcwin_media_session_smoke.exe
```

Two-client camera smoke (A publishes camera, B receives through muxPlayer):

```powershell
$env:RTCWIN_TRACE_EVENTS = '1'
$env:PATH = "$env:QTDIR\bin;" + $env:PATH
.\build\windows-x64\bin\Release\rtc-talk.exe `
  --auto-join-url ws://47.117.159.193:8080/talk/v1/ws `
  --auto-join-room rtcwin-video-smoke-a `
  --auto-join-uid video-a `
  --auto-join-dev-token `
  --auto-join-backend fake `
  --auto-join-media-mode sfu `
  --auto-camera-ms 3500
```

Launch a second client in the same room with a different UID. Use distinct
`RTCWIN_LOG_PATH` values when collecting diagnostics.

## S4.2 screen sharing and P2P video

Automated screen-sharing flags:

```powershell
--auto-screen-ms 3000
--auto-screen-stop-ms 7000
--auto-camera-stop-ms 9000
```

For P2P validation, launch both clients with:

```powershell
--auto-join-media-mode p2p
```

The client now pre-creates `audio/camera/screen` m-lines for P2P and advertises
`caps.p2p_video=true`. Screen capture uses libwebrtc DesktopCapturer with
WGC/DXGI and converts BGRA to I420 before publishing.

Expected evidence:

- Sender `screen_frames` and `screen_sender` increase.
- SFU/P2P receiver `video_packets_received` increases.
- Stopping screen sharing sends `media.setMute{source:"screen",muted:true}` and
  the remote shared tile clears without waiting for RTP timeout.

## S4.3 M1 video scene and PIP

`rtcwin_video_scene_tests` covers the application-layer scene policy and the
UI-layer layout policy:

- camera and screen are independent `uid + source` scene items;
- Shared mode selects the screen as the main item;
- a sharing member keeps a camera PIP item;
- Grid, Focus, and Shared produce deterministic layout plans.

The Qt render check can be run locally after the Release build:

```powershell
$env:QTDIR = 'C:\Qt\5.15.2\msvc2019_64'
$env:PATH = "$env:QTDIR\bin;$PWD\build\windows-x64\bin\Release;$env:PATH"
.\build\windows-x64\bin\Release\rtc-talk.exe `
  --auto-preview --size 1440x900 `
  --screenshot .\build\windows-x64\m1-camera-preview.png `
  --screenshot-delay-ms 4000
```

For the SFU dual-stream check, the receiver should show the screen as the main
tile and the sharing member's camera as a rounded PIP. The sender and receiver
must use different `RTCWIN_LOG_PATH` values when collecting diagnostics.
## S4.4 M2 multi-surface and frame health

`rtcwin_frame_health_tests` verifies:

- no frame -> Waiting;
- frame within 5 seconds -> Healthy;
- no frame for 5 seconds -> Stale;
- producer paused -> Paused;
- peer left/removed -> Removed;
- a new frame resets stale state.

`rtcwin_video_surface_pool_tests` verifies:

- maximum active surface capacity;
- RAII lease release;
- idle surface reuse;
- no duplicate lease for one stream key;
- shutdown clears active and idle references.

`rtcwin_video_grid_stress_tests` verifies:

- 9 camera streams create 9 tiles and 9 independent pool surfaces;
- 9 x 1280x720 frame intake are cached under distinct `uid + source` keys;
- disabling one stream returns its surface to the pool;
- re-enabling the stream reuses the surface;
- 100 visibility/layout cycles do not grow the surface count.

Current reference result on the development machine:

```text
nine_streams_720p_intake_ms=29
video_grid_stress_test: PASS
```

Real断流验证：暂停发送端进程后，接收端在 5 秒后清空共享画面并释放对应
Surface，但没有伪造 peerLeft 或 producer paused 事件。

## S6 M4 device and screen-source smoke

The room `设备` page now supports microphone, playout, system-audio loopback,
camera, and screen/window source selection. Device lists are refreshed every
five seconds while joined; if a selected device disappears, the application
falls back to the first available device.

Useful automation flags:

```text
--auto-select-delay-ms 4000
--auto-select-recording-id <endpoint-id>
--auto-select-playout-id <endpoint-id>
--auto-select-system-audio-id <render-endpoint-id>
--auto-select-camera-index 0
--auto-select-screen-source-id <desktop-source-id>
--auto-select-screen-source-window
```

Desktop sources can be inspected from the event trace after the UI starts:

```text
desktop.sources -> sources[{id,title,type}]
```

Validated M4 evidence:

- WASAPI microphone, playout, and camera switched during a joined room without
  reconnecting.
- Screen sharing switched from the default screen to a selected window while
  capture continued.
- System audio switched from the default endpoint to a selected loopback
  endpoint.
- An invalid selected microphone was automatically replaced after the periodic
  device refresh.
- A disabled screen producer is explicitly paused after initial SFU publish, so
  receivers no longer show a false `共享中` state.

## S7 M5 resume and ICE recovery smoke

Automation flags:

```text
--auto-reconnect-ms 4000
--auto-refresh-ice-ms 3000
--auto-restart-ice-pc send
```

`auto-reconnect-ms` intentionally aborts the active signaling transport. The
Engine then applies the reconnect backoff, sends `session.hello` with
`resume.session_id`, and restores the room snapshot without resetting media on a
successful resume.

`auto-refresh-ice-ms` refreshes TURN credentials. With
`--auto-restart-ice-pc send`, the client requests `sfu.restartIce` and then
re-generates/re-publishes the SFU send offer with ICE restart enabled.

Validated evidence:

- Single-client resume completed after one-second backoff.
- Two-client resume preserved the member list and produced no peerLeft.
- Room ID and host role were preserved after resume.
- ICE refresh returned new TURN credentials.
- SFU send restart produced a second `media.publishing` / `media.published`
  sequence.

## S6 M6 statistics and diagnostics

The status page consumes `media.stats` every five seconds and displays:

- RTT (`rtt_ms`)
- jitter (`jitter_ms`)
- loss (`loss_percent`)
- total bitrate (`bitrate_bps`)
- selected ICE candidate pair

Diagnostic export can be triggered from the status page or with:

```text
--auto-export-diagnostics-ms 6500
--auto-room-tab status
```

The ZIP contains `manifest.json`, `session.json`, `logs/rtcwin.log`, and a
README. Session and log data are sanitized: tokens, TURN credentials, SDP,
ICE candidates, raw endpoint IDs, and raw candidate addresses are not written.
