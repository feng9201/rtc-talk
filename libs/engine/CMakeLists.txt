add_library(rtcwin_engine SHARED
  include/rtcwin/engine/engine_core.h
  include/rtcwin/engine/engine_command.h
  include/rtcwin/engine/engine_command_router.h
  include/rtcwin/engine/engine_event_sink.h
  include/rtcwin/engine/media_orchestrator.h
  include/rtcwin/engine/reconnect_controller.h
  include/rtcwin/engine/session_controller.h
  include/rtcwin/engine/session_state_machine.h
  include/rtcwin/engine/signaling_transport.h
  src/engine_command_router.cpp
  src/engine_event_sink.cpp
  src/media_orchestrator.cpp
  src/reconnect_controller.cpp
  src/session_controller.cpp
  src/session_state_machine.cpp
  src/signaling_transport.cpp
  src/engine_log.h
  src/engine_log.cpp
  src/engine_core.cpp
  src/client_api.cpp
)

target_include_directories(rtcwin_engine
  PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
  PRIVATE
    "${CMAKE_SOURCE_DIR}/libs/client_api/include"
    "${CMAKE_SOURCE_DIR}/libs/camera_win/include"
)

target_compile_definitions(rtcwin_engine PRIVATE
  RTCWIN_ENGINE_EXPORTS
  FMT_HEADER_ONLY
  $<$<CONFIG:Debug>:_HAS_ITERATOR_DEBUGGING=0>
)
target_compile_options(rtcwin_engine PRIVATE
  $<$<CXX_COMPILER_ID:MSVC>:/utf-8 /EHsc /GR /W3>
)
target_link_libraries(rtcwin_engine PRIVATE
  rtcwin_camera_win
  rtcwin_signaling
  rtcwin_webrtc_media
  rtcwin_log_adapter
)
set_target_properties(rtcwin_engine PROPERTIES
  CXX_STANDARD 17
  CXX_STANDARD_REQUIRED ON
  MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
)

# Copy every runtime DLL required by the Engine dependency graph (C ABI adapter,
# vcpkg log-cpp, spdlog/fmt, etc.) next to the Engine.
add_custom_command(TARGET rtcwin_engine POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          $<TARGET_RUNTIME_DLLS:rtcwin_engine>
          $<TARGET_FILE_DIR:rtcwin_engine>
  COMMAND_EXPAND_LISTS
)
