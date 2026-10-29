add_executable(rtcwin_tests domain_application_test.cpp)

target_link_libraries(rtcwin_tests PRIVATE
  rtcwin_application
  rtcwin_engine
  rtcwin_client_api
  Qt5::Core
)

target_compile_options(rtcwin_tests PRIVATE
  $<$<CXX_COMPILER_ID:MSVC>:/utf-8 /EHsc /GR /W3>
)

set_target_properties(rtcwin_tests PROPERTIES
  CXX_STANDARD 17
  CXX_STANDARD_REQUIRED ON
  MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL"
)

add_test(NAME rtcwin_tests COMMAND rtcwin_tests)

add_executable(rtcwin_session_state_tests
  session_state_machine_test.cpp
  ../libs/engine/src/session_state_machine.cpp
)
target_include_directories(rtcwin_session_state_tests PRIVATE
  ../libs/engine/include
)
target_compile_options(rtcwin_session_state_tests PRIVATE
  $<$<CXX_COMPILER_ID:MSVC>:/utf-8 /EHsc /GR /W3>
)
set_target_properties(rtcwin_session_state_tests PROPERTIES
  CXX_STANDARD 17
  CXX_STANDARD_REQUIRED ON
  MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL"
)
add_test(NAME rtcwin_session_state_tests COMMAND rtcwin_session_state_tests)

add_executable(rtcwin_camera_preview_smoke camera_preview_smoke.cpp)

target_link_libraries(rtcwin_camera_preview_smoke PRIVATE
  rtcwin_client_api
  rtcwin_engine
)

target_compile_options(rtcwin_camera_preview_smoke PRIVATE
  $<$<CXX_COMPILER_ID:MSVC>:/utf-8 /EHsc /GR /W3>
)

set_target_properties(rtcwin_camera_preview_smoke PROPERTIES
  CXX_STANDARD 17
  CXX_STANDARD_REQUIRED ON
  MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
)

add_custom_command(TARGET rtcwin_camera_preview_smoke POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          $<TARGET_FILE:rtcwin_engine>
          $<TARGET_FILE_DIR:rtcwin_camera_preview_smoke>
)
add_executable(rtcwin_signaling_tests signaling_protocol_test.cpp)
target_link_libraries(rtcwin_signaling_tests PRIVATE rtcwin_signaling)
target_compile_options(rtcwin_signaling_tests PRIVATE
  $<$<CXX_COMPILER_ID:MSVC>:/utf-8 /EHsc /GR /W3>
)
set_target_properties(rtcwin_signaling_tests PROPERTIES
  CXX_STANDARD 17
  CXX_STANDARD_REQUIRED ON
  MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
)
add_test(NAME rtcwin_signaling_tests COMMAND rtcwin_signaling_tests)

add_executable(rtcwin_log_adapter_smoke log_adapter_smoke.cpp)
target_link_libraries(rtcwin_log_adapter_smoke PRIVATE rtcwin_log_adapter)
target_compile_options(rtcwin_log_adapter_smoke PRIVATE
  $<$<CXX_COMPILER_ID:MSVC>:/utf-8 /EHsc /GR /W3>
)
set_target_properties(rtcwin_log_adapter_smoke PROPERTIES
  CXX_STANDARD 17
  CXX_STANDARD_REQUIRED ON
  MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL"
)
add_test(NAME rtcwin_log_adapter_smoke COMMAND rtcwin_log_adapter_smoke)

add_executable(rtcwin_signaling_smoke signaling_smoke.cpp)
target_link_libraries(rtcwin_signaling_smoke PRIVATE rtcwin_signaling)
target_compile_options(rtcwin_signaling_smoke PRIVATE
  $<$<CXX_COMPILER_ID:MSVC>:/utf-8 /EHsc /GR /W3>
)
set_target_properties(rtcwin_signaling_smoke PROPERTIES
  CXX_STANDARD 17
  CXX_STANDARD_REQUIRED ON
  MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
)
if(RTCWIN_BUILD_WEBRTC_MEDIA)
  add_executable(rtcwin_audio_devices_probe audio_devices_probe.cpp)
  target_link_libraries(rtcwin_audio_devices_probe PRIVATE rtcwin_webrtc_media)
  target_compile_options(rtcwin_audio_devices_probe PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/utf-8 /std:c++20 /EHsc /GR /W3>
  )
  set_target_properties(rtcwin_audio_devices_probe PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
  )
endif()
if(RTCWIN_BUILD_WEBRTC_MEDIA)
  add_executable(rtcwin_media_session_smoke media_session_smoke.cpp)
  target_link_libraries(rtcwin_media_session_smoke PRIVATE rtcwin_webrtc_media)
  target_compile_options(rtcwin_media_session_smoke PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/utf-8 /std:c++20 /EHsc /GR /W3>
  )
  set_target_properties(rtcwin_media_session_smoke PROPERTIES
    CXX_STANDARD 20
    CXX_STANDARD_REQUIRED ON
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
  )
  add_test(NAME rtcwin_media_session_smoke COMMAND rtcwin_media_session_smoke)
endif()
