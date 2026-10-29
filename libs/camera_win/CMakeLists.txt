add_library(rtcwin_camera_win STATIC
  include/rtcwin/camera_win/camera_capture.h
  src/camera_capture.cpp
  src/internal/com_pointer.h
  src/internal/mf_capture.h
  src/internal/mf_capture.cpp
)

target_include_directories(rtcwin_camera_win
  PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
  PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src/internal"
)

target_compile_definitions(rtcwin_camera_win PRIVATE
  WIN32_LEAN_AND_MEAN
  NOMINMAX
  _WIN32_WINNT=0x0A00
  $<$<CONFIG:Debug>:_HAS_ITERATOR_DEBUGGING=0>
)

target_compile_options(rtcwin_camera_win PRIVATE
  $<$<CXX_COMPILER_ID:MSVC>:/utf-8 /EHsc /GR /W3>
)

target_link_libraries(rtcwin_camera_win PRIVATE
  mf
  mfplat
  mfuuid
  mfreadwrite
  ole32
)

set_target_properties(rtcwin_camera_win PROPERTIES
  CXX_STANDARD 17
  CXX_STANDARD_REQUIRED ON
  MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
)
