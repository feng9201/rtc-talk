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

The full CTest set is currently five tests:

- `rtcwin_tests`
- `rtcwin_session_state_tests`
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
`RTCWIN_LOG_PATH` values when collecting diagnostics. S4.1 is camera-only;
screen sharing and P2P video remain future work.
