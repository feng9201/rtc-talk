add_library(rtcwin_webrtc_media STATIC
  include/rtcwin/webrtc_media/audio_device_service.h
  include/rtcwin/webrtc_media/loopback_audio_capture.h
  include/rtcwin/webrtc_media/media_session.h
  include/rtcwin/webrtc_media/switchable_audio_device.h
  src/audio_device_service.cpp
  src/loopback_audio_capture.cpp
  src/media_session.cpp
  src/remote_video_sink.cpp
  src/remote_video_sink.h
  src/switchable_audio_device.cpp
  src/video_track_source.cpp
  src/video_track_source.h
  src/video/h264_bitstream.cpp
  src/video/h264_bitstream.h
  src/video/mf_h264_codec.cpp
  src/video/mf_h264_codec.h
  src/video/webrtc_h264_factory.cpp
  src/video/webrtc_h264_factory.h
)

target_include_directories(rtcwin_webrtc_media
  PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(rtcwin_webrtc_media PRIVATE
  rtcwin_webrtc
  mf
  mfplat
  mfuuid
  mfreadwrite
  ole32
)

target_compile_definitions(rtcwin_webrtc_media
  PUBLIC $<$<CONFIG:Debug>:_HAS_ITERATOR_DEBUGGING=0>
  PRIVATE _HAS_ITERATOR_DEBUGGING=0
)

target_compile_options(rtcwin_webrtc_media PRIVATE
  $<$<CXX_COMPILER_ID:MSVC>:/utf-8 /std:c++20 /EHs-c- /GR /W3>
)

set_target_properties(rtcwin_webrtc_media PROPERTIES
  CXX_STANDARD 20
  CXX_STANDARD_REQUIRED ON
  MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
)
