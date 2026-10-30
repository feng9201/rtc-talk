add_library(rtcwin_application STATIC
  include/rtcwin/application/app_controller.h
  include/rtcwin/application/engine_client.h
  include/rtcwin/application/frame_health_monitor.h
  include/rtcwin/application/settings_store.h
  include/rtcwin/application/unavailable_engine_client.h
  include/rtcwin/application/video_scene.h
  src/app_controller.cpp
  src/frame_health_monitor.cpp
  src/settings_store.cpp
  src/unavailable_engine_client.cpp
  src/video_scene.cpp
)

target_include_directories(rtcwin_application PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(rtcwin_application
  PUBLIC
    rtcwin_domain
    Qt5::Core
)

target_compile_options(rtcwin_application PRIVATE
  $<$<CXX_COMPILER_ID:MSVC>:/utf-8 /EHsc /GR /W3>
)

set_target_properties(rtcwin_application PROPERTIES
  CXX_STANDARD 17
  CXX_STANDARD_REQUIRED ON
  AUTOMOC ON
  MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL"
)
