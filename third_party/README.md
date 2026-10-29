# Bundled third-party dependencies

## muxPlayer

`third_party/muxplayer` contains the complete user-wrapped muxPlayer source tree
from `device_tool` commit `4c3da7bf8ec92c0826e143918403cba5d61ab1db`:

- full multiplex player, source, decode, demux, render, display, and pipeline code
- original upstream build file preserved as `CMakeLists.upstream.txt`
- project integration build file at `CMakeLists.txt`
- S1 currently builds only `muxplayer_render`, a minimal raw-I420 software-GL path
- the complete WGL/D3D11 zero-copy implementation remains in-tree for later use

## Bundled user libraries required by the full muxPlayer build

- `third_party/mtAudio`
- `third_party/mtAudioPlayer`
- `third_party/tmcore`
- `third_party/flvparser`

## External tools and package sources

libwebrtc is an external SDK resolved through `WEBRTC_SDK_DIR`. Qt and Visual Studio are development environments and are intentionally not
vendored. FFmpeg and other muxPlayer package dependencies are obtained from the
private vcpkg registry configured in `vcpkg.json` and
`vcpkg-configuration.json`.

Expected package dependencies for the future full muxPlayer target:

- FFmpeg
- mttool
- log-cpp
- fdk-aac
- Opus
- cpr / OpenSSL for HTTP-FLV
- miniaudio