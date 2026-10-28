# muxPlayer integration patches

This directory contains the complete source snapshot from muxPlayer commit
`4c3da7bf8ec92c0826e143918403cba5d61ab1db`. It is not built from the provider
working tree.

## Integration changes

1. Added an independent `muxplayer_render` CMake target with explicit sources,
   Qt 5.15, OpenGL, and `libavutil` dependencies.
2. Added `GLPlayWidget::setSoftwareFramePump(FramePump*)` so an external raw-frame
   producer can own frame lifetime.
3. Added `FramePump::Clear()` to drop the latest frame when preview stops.
4. Added an upper-layer `rtcwin_muxplayer_adapter::MuxVideoSurface` boundary;
   UI code does not include muxPlayer private headers.
5. The current build enables only the software OpenGL path. The WGL/D3D11
   zero-copy implementation and its DLL dependencies are intentionally not part
   of the S1 build, but the `MuxVideoSurface` API is the future backend boundary.

## SHA256 inventory

```text
e1f72476e3a3617b4367ef0e5a53789cb4d85da3ed4ecb7bc0c351f829d59d15  CMakeLists.txt
905798785ff4c24c2c84903dd63c7b27df348c9f284bca1cad4701e6dd26cd14  display/gl_yuv_shader.cpp
a281f959fdec98f6acc5c3b74387dfef554b1665c5b173e0c8cdcce722a0e668  display/gl_yuv_shader.h
b5db983ea099d10a1e78aface636a58e77eab55dee7ae271bc37dd2ce70b641c  display/soft_gl_display.cpp
fdbfd25a2fab9e3a2899eaf1fcbaff80c98bd6147efa7806a7d32930228e955b  display/soft_gl_display.h
705ca25c85f5dfe6142dddfcb03a32af31adf80e749a8493163c30eb0f5b1f1d  include/gl_play_widget.h
35189f5f6ab6be26d78d2ae2ca204e9b070aa9cfd92706cf38f6aaf1c3225f29  pipeline/frame_types.h
ac538faf058a81804585aae5206a73ec477afcfc6c0b4fb599e03e95a155021a  pipeline/idisplay_consumer.h
7b0d1c68809b74c8c998d723bdafff1be74631cd3a799e0cdd202778a69fcd76  pipeline/iframe_consumer.h
1f8673b193bdea9f8b7ee3f158eff0fbc9b1a013aa89a044b802b88553e0ee4d  render/frame_pump.cpp
6a9030e5857a57f33430708b9b41ae210f6f8c6746f3dcd26eaafa4fcbbc95c4  render/frame_pump.h
f80ebd4bcb5b62dc5fcee665e4341e31c39ab6feefc75719c81a542fb6e2e6cf  render/gl_play_widget.cpp
```