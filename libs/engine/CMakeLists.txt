add_library(rtcwin_engine SHARED
  include/rtcwin/engine/engine_core.h
  src/engine_core.cpp
  src/client_api.cpp
)

target_include_directories(rtcwin_engine
  PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
  PRIVATE
    "${CMAKE_SOURCE_DIR}/libs/client_api/include"
    "${CMAKE_SOURCE_DIR}/libs/camera_win/include"
)

target_compile_definitions(rtcwin_engine PRIVATE RTCWIN_ENGINE_EXPORTS)
target_compile_options(rtcwin_engine PRIVATE
  $<$<CXX_COMPILER_ID:MSVC>:/utf-8 /EHsc /GR /W3>
)
target_link_libraries(rtcwin_engine PRIVATE rtcwin_camera_win)
set_target_properties(rtcwin_engine PROPERTIES
  CXX_STANDARD 17
  CXX_STANDARD_REQUIRED ON
  MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
)
