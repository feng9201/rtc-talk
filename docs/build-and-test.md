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

The S1 build currently enables the raw-I420 software OpenGL renderer only. Full
muxPlayer WGL/D3D11 zero-copy sources remain in-tree and will be enabled in a
later stage with their package dependencies.

## Environment

```powershell
$env:VCPKG_ROOT = 'D:\vcpkg'
$env:QTDIR = 'C:\Qt\5.15.2\msvc2019_64'
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

## S1 boundaries

- Current renderer: CPU I420 -> muxPlayer software OpenGL
- WebRTC signaling and encoding are not part of S1
- Local preview does not decode a loopback H.264 stream
- WGL/D3D11 zero-copy remains the required follow-up rendering backend