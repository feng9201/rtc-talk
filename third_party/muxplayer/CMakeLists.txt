add_library(muxplayer_render STATIC
  include/gl_play_widget.h
  display/gl_yuv_shader.h
  display/gl_yuv_shader.cpp
  display/soft_gl_display.h
  display/soft_gl_display.cpp
  pipeline/frame_types.h
  pipeline/idisplay_consumer.h
  pipeline/iframe_consumer.h
  render/frame_pump.h
  render/frame_pump.cpp
  render/gl_play_widget.cpp
)

target_include_directories(muxplayer_render
  PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_CURRENT_SOURCE_DIR}/render"
    ${FFMPEG_INCLUDE_DIRS}
  PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/display"
    "${CMAKE_CURRENT_SOURCE_DIR}/pipeline"
)

target_compile_definitions(muxplayer_render PUBLIC ENABLE_WGL_INTEROP=0)
target_compile_options(muxplayer_render PRIVATE
  $<$<CXX_COMPILER_ID:MSVC>:/utf-8 /EHsc /GR /W3>
)

target_link_libraries(muxplayer_render
  PUBLIC
    Qt5::Core
    Qt5::Gui
    Qt5::Widgets
    Qt5::OpenGL
    OpenGL::GL
    ${FFMPEG_libavutil_LIBRARY}
)

set_target_properties(muxplayer_render PROPERTIES
  CXX_STANDARD 17
  CXX_STANDARD_REQUIRED ON
  AUTOMOC ON
  MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL"
)
