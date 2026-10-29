# rtc-talk-win

Windows x64 production RTC desktop client.

## Bundled product dependencies

The complete user-wrapped muxPlayer source tree is stored in
`third_party/muxplayer`. Its user libraries `mtAudio`, `mtAudioPlayer`, `tmcore`,
and `flvparser` are also bundled under `third_party`.

libwebrtc is integrated as an external development SDK through `WEBRTC_SDK_DIR`. Qt and Visual Studio are development environments. FFmpeg and other package
dependencies are resolved from the private vcpkg registry configured by
`vcpkg.json` and `vcpkg-configuration.json`.

## Build

```powershell
$env:VCPKG_ROOT = 'D:\vcpkg'
$env:QTDIR = 'C:\Qt\5.15.2\msvc2019_64'
$env:WEBRTC_SDK_DIR = 'D:\google\webrtc_sdk_hevc'
cmake --preset windows-x64
cmake --build --preset windows-x64-release --parallel
ctest --test-dir build/windows-x64 -C Release --output-on-failure
```

## Run

```powershell
$env:PATH = "$env:QTDIR\bin;" + $env:PATH
.\build\windows-x64\bin\Release\rtc-talk.exe
```

Detailed build, dependency, and S1 boundary documentation is in
`docs/build-and-test.md`.