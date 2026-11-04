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

add_executable(rtcwin_capture_settings_tests capture_settings_test.cpp)
target_link_libraries(rtcwin_capture_settings_tests PRIVATE rtcwin_ui)
target_compile_options(rtcwin_capture_settings_tests PRIVATE /utf-8 /EHsc /W3)
set_target_properties(rtcwin_capture_settings_tests PROPERTIES
  MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
add_test(NAME rtcwin_capture_settings_tests COMMAND rtcwin_capture_settings_tests -platform offscreen)
set_tests_properties(rtcwin_capture_settings_tests PROPERTIES TIMEOUT 15)

add_executable(rtcwin_device_combo_tests device_combo_box_test.cpp)
target_link_libraries(rtcwin_device_combo_tests PRIVATE rtcwin_ui)
target_compile_options(rtcwin_device_combo_tests PRIVATE /utf-8 /EHsc /W3)
set_target_properties(rtcwin_device_combo_tests PROPERTIES
  MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
add_test(NAME rtcwin_device_combo_tests COMMAND rtcwin_device_combo_tests -platform offscreen)
set_tests_properties(rtcwin_device_combo_tests PROPERTIES TIMEOUT 15)

add_executable(rtcwin_join_error_bubble_tests join_error_bubble_test.cpp)
target_include_directories(rtcwin_join_error_bubble_tests PRIVATE "${CMAKE_SOURCE_DIR}/libs/ui/src")
target_link_libraries(rtcwin_join_error_bubble_tests PRIVATE rtcwin_ui)
target_compile_options(rtcwin_join_error_bubble_tests PRIVATE /utf-8 /EHsc /W3)
set_target_properties(rtcwin_join_error_bubble_tests PROPERTIES
  MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
add_test(NAME rtcwin_join_error_bubble_tests COMMAND rtcwin_join_error_bubble_tests -platform offscreen)
get_filename_component(rtcwin_qt_platform_plugins "${Qt5_DIR}/../../../plugins/platforms" ABSOLUTE)
set_tests_properties(rtcwin_capture_settings_tests PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM_PLUGIN_PATH=${rtcwin_qt_platform_plugins}")
set_tests_properties(rtcwin_device_combo_tests PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM_PLUGIN_PATH=${rtcwin_qt_platform_plugins}")
set_tests_properties(rtcwin_join_error_bubble_tests PROPERTIES
  ENVIRONMENT "QT_QPA_PLATFORM_PLUGIN_PATH=${rtcwin_qt_platform_plugins}"
  TIMEOUT 30)

add_executable(rtcwin_crash_handler_smoke crash_handler_smoke.cpp)
target_link_libraries(rtcwin_crash_handler_smoke PRIVATE rtcwin_crash_handler dbghelp)
target_compile_definitions(rtcwin_crash_handler_smoke PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(rtcwin_crash_handler_smoke PRIVATE /utf-8 /EHsc /W3)
set_target_properties(rtcwin_crash_handler_smoke PROPERTIES
  MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
add_test(NAME rtcwin_crash_handler_smoke COMMAND rtcwin_crash_handler_smoke
  "${CMAKE_BINARY_DIR}/crash-handler-smoke/$<CONFIG>")
set_tests_properties(rtcwin_crash_handler_smoke PROPERTIES TIMEOUT 45)

add_executable(rtcwin_credential_store_tests credential_store_test.cpp)
target_link_libraries(rtcwin_credential_store_tests PRIVATE
  rtcwin_application
  Qt5::Core
)
target_compile_options(rtcwin_credential_store_tests PRIVATE
  $<$<CXX_COMPILER_ID:MSVC>:/utf-8 /EHsc /GR /W3>
)
set_target_properties(rtcwin_credential_store_tests PROPERTIES
  CXX_STANDARD 17
  CXX_STANDARD_REQUIRED ON
  MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL"
)
add_test(NAME rtcwin_credential_store_tests COMMAND rtcwin_credential_store_tests)

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

add_executable(rtcwin_video_scene_tests
  video_scene_test.cpp
  ../libs/ui/src/video/video_layout.cpp
)
target_include_directories(rtcwin_video_scene_tests PRIVATE
  ../libs/application/include
  ../libs/ui/include
  ../libs/domain/include
)
target_link_libraries(rtcwin_video_scene_tests PRIVATE rtcwin_application)
target_compile_options(rtcwin_video_scene_tests PRIVATE
  $<$<CXX_COMPILER_ID:MSVC>:/utf-8 /EHsc /GR /W3>
)
set_target_properties(rtcwin_video_scene_tests PROPERTIES
  CXX_STANDARD 17
  CXX_STANDARD_REQUIRED ON
  MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL"
)
add_test(NAME rtcwin_video_scene_tests COMMAND rtcwin_video_scene_tests)

add_executable(rtcwin_frame_health_tests frame_health_monitor_test.cpp)
target_link_libraries(rtcwin_frame_health_tests PRIVATE rtcwin_application)
target_compile_options(rtcwin_frame_health_tests PRIVATE
  $<$<CXX_COMPILER_ID:MSVC>:/utf-8 /EHsc /GR /W3>
)
set_target_properties(rtcwin_frame_health_tests PROPERTIES
  CXX_STANDARD 17
  CXX_STANDARD_REQUIRED ON
  MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL"
)
add_test(NAME rtcwin_frame_health_tests COMMAND rtcwin_frame_health_tests)

add_executable(rtcwin_floor_host_controller_tests
  floor_host_controller_test.cpp
)
target_link_libraries(rtcwin_floor_host_controller_tests PRIVATE
  rtcwin_application
)
target_compile_options(rtcwin_floor_host_controller_tests PRIVATE
  $<$<CXX_COMPILER_ID:MSVC>:/utf-8 /EHsc /GR /W3>
)
set_target_properties(rtcwin_floor_host_controller_tests PROPERTIES
  CXX_STANDARD 17
  CXX_STANDARD_REQUIRED ON
  MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL"
)
add_test(NAME rtcwin_floor_host_controller_tests
  COMMAND rtcwin_floor_host_controller_tests)

add_executable(rtcwin_reconnect_controller_tests
  reconnect_controller_test.cpp
  ../libs/engine/src/reconnect_controller.cpp
)
target_include_directories(rtcwin_reconnect_controller_tests PRIVATE
  ../libs/engine/include
)
target_compile_options(rtcwin_reconnect_controller_tests PRIVATE
  $<$<CXX_COMPILER_ID:MSVC>:/utf-8 /EHsc /GR /W3>
)
set_target_properties(rtcwin_reconnect_controller_tests PROPERTIES
  CXX_STANDARD 17
  CXX_STANDARD_REQUIRED ON
  MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL"
)
add_test(NAME rtcwin_reconnect_controller_tests
  COMMAND rtcwin_reconnect_controller_tests)

add_executable(rtcwin_video_surface_pool_tests video_surface_pool_test.cpp)
target_link_libraries(rtcwin_video_surface_pool_tests PRIVATE
  rtcwin_ui
  Qt5::Widgets
)
target_compile_definitions(rtcwin_video_surface_pool_tests PRIVATE
  $<$<CXX_COMPILER_ID:MSVC>:_SILENCE_STDEXT_ARR_ITERS_DEPRECATION_WARNING>
)
target_compile_options(rtcwin_video_surface_pool_tests PRIVATE
  $<$<CXX_COMPILER_ID:MSVC>:/utf-8 /EHsc /GR /W3>
)
set_target_properties(rtcwin_video_surface_pool_tests PROPERTIES
  CXX_STANDARD 17
  CXX_STANDARD_REQUIRED ON
  MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL"
)
add_test(NAME rtcwin_video_surface_pool_tests COMMAND rtcwin_video_surface_pool_tests)

add_executable(rtcwin_video_grid_stress_tests video_grid_stress_test.cpp)
target_link_libraries(rtcwin_video_grid_stress_tests PRIVATE
  rtcwin_ui
  Qt5::Widgets
)
target_compile_definitions(rtcwin_video_grid_stress_tests PRIVATE
  $<$<CXX_COMPILER_ID:MSVC>:_SILENCE_STDEXT_ARR_ITERS_DEPRECATION_WARNING>
)
target_compile_options(rtcwin_video_grid_stress_tests PRIVATE
  $<$<CXX_COMPILER_ID:MSVC>:/utf-8 /EHsc /GR /W3>
)
set_target_properties(rtcwin_video_grid_stress_tests PROPERTIES
  CXX_STANDARD 17
  CXX_STANDARD_REQUIRED ON
  MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL"
)
add_test(NAME rtcwin_video_grid_stress_tests COMMAND rtcwin_video_grid_stress_tests)

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

add_executable(rtcwin_wss_transport_smoke wss_transport_smoke.cpp)
target_link_libraries(rtcwin_wss_transport_smoke PRIVATE rtcwin_signaling)
target_compile_options(rtcwin_wss_transport_smoke PRIVATE
  $<$<CXX_COMPILER_ID:MSVC>:/utf-8 /EHsc /GR /W3>
)
set_target_properties(rtcwin_wss_transport_smoke PROPERTIES
  CXX_STANDARD 17
  CXX_STANDARD_REQUIRED ON
  MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
)

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
  add_executable(rtcwin_sfu_early_offer_test sfu_early_offer_test.cpp)
  target_link_libraries(rtcwin_sfu_early_offer_test PRIVATE
    rtcwin_engine rtcwin_client_api rtcwin_signaling rtcwin_webrtc_media ws2_32 mswsock)
  set_target_properties(rtcwin_sfu_early_offer_test PROPERTIES
    CXX_STANDARD 17 CXX_STANDARD_REQUIRED ON
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  target_compile_options(rtcwin_sfu_early_offer_test PRIVATE /utf-8 /EHsc)
  add_test(NAME rtcwin_sfu_early_offer_test COMMAND rtcwin_sfu_early_offer_test)
  set_tests_properties(rtcwin_sfu_early_offer_test PROPERTIES TIMEOUT 15)
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
  add_executable(rtcwin_capture_resolution_smoke capture_resolution_smoke.cpp)
  target_link_libraries(rtcwin_capture_resolution_smoke PRIVATE rtcwin_webrtc_media rtcwin_webrtc)
  target_include_directories(rtcwin_capture_resolution_smoke PRIVATE "${CMAKE_SOURCE_DIR}/libs/webrtc_media/src")
  target_compile_options(rtcwin_capture_resolution_smoke PRIVATE /utf-8 /EHsc /GR /W3)
  set_target_properties(rtcwin_capture_resolution_smoke PROPERTIES CXX_STANDARD 20
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
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
