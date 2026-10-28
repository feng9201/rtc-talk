add_library(rtcwin_muxplayer_adapter STATIC
  include/rtcwin/muxplayer_adapter/mux_video_surface.h
  src/mux_video_surface.cpp
)

target_include_directories(rtcwin_muxplayer_adapter PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(rtcwin_muxplayer_adapter
  PUBLIC rtcwin_domain Qt5::Widgets muxplayer_render
)

target_compile_options(rtcwin_muxplayer_adapter PRIVATE
  $<$<CXX_COMPILER_ID:MSVC>:/utf-8 /EHsc /GR /W3>
)

set_target_properties(rtcwin_muxplayer_adapter PROPERTIES
  CXX_STANDARD 17
  CXX_STANDARD_REQUIRED ON
  AUTOMOC ON
  MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL"
)
