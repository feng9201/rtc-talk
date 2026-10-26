cmake_minimum_required(VERSION 3.24)
project(rtc_talk_client_win VERSION 0.1.0 LANGUAGES C CXX)

cmake_policy(SET CMP0091 NEW) # MSVC_RUNTIME_LIBRARY 按 target 设置

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(TALK_WITH_WEBRTC "构建 libwebrtc 媒体引擎（/MT）" ON)
option(TALK_WITH_QT "构建 Qt5 界面（/MD，仅走 C API）" ON)
option(TALK_BUILD_DESKTOP "构建生产 Qt 桌面客户端" ON)
option(TALK_BUILD_LEGACY_QT_DEMO "构建旧版 talk-qt 联调界面" ON)
option(TALK_BUILD_TESTS "构建测试" ON)

if(NOT WIN32)
  message(FATAL_ERROR "本工程仅支持 Windows")
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
foreach(cfg Debug Release RelWithDebInfo MinSizeRel)
  string(TOUPPER "${cfg}" cfg_upper)
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${cfg_upper} "${CMAKE_BINARY_DIR}/bin/${cfg}")
  set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${cfg_upper} "${CMAKE_BINARY_DIR}/lib/${cfg}")
  set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${cfg_upper} "${CMAKE_BINARY_DIR}/bin/${cfg}")
endforeach()

find_package(nlohmann_json CONFIG REQUIRED)
find_package(asio CONFIG REQUIRED)
find_package(log-cpp CONFIG REQUIRED)

# 协议类型与 JSON 工具（仅头文件，CRT 无关）。
add_library(talk_protocol INTERFACE)
target_include_directories(talk_protocol INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/core")
target_link_libraries(talk_protocol INTERFACE nlohmann_json::nlohmann_json)

# 媒体实现：/MT + 关闭异常，与 libwebrtc 一致；不向 core 传播 webrtc 编译宏。
set(TALK_HAS_MEDIA OFF)
if(TALK_WITH_WEBRTC)
  include(cmake/webrtc.cmake)
  add_library(talk_media STATIC
    core/media/webrtc/audio_device.cpp
    core/media/webrtc/capture_gate.cpp
    core/media/webrtc/loopback_audio_capture.cpp
    core/media/webrtc/media_environment.cpp
    core/media/webrtc/media_stats.cpp
    core/media/webrtc/peer_connection.cpp
    core/media/webrtc/sine_audio_device.cpp
    core/media/webrtc/switchable_audio_device.cpp
    core/media/webrtc/webrtc_media.cpp
    core/media/webrtc/video/camera_video_source.cpp
    core/media/webrtc/video/desktop_video_source.cpp
    core/media/webrtc/video/mf_capture.cpp
    core/media/webrtc/video/h264_bitstream.cpp
    core/media/webrtc/video/mf_h264_codec.cpp
    core/media/webrtc/video/video_frame_sink.cpp
    core/media/webrtc/video/webrtc_h264_factory.cpp
  )
  target_link_libraries(talk_media PRIVATE talk_protocol talk_webrtc)
  # libyuv 头文件内部以 "libyuv/xxx.h" 引用，需要其 include 根目录。
  target_include_directories(talk_media PRIVATE
    "${WEBRTC_SDK_DIR}/include/third_party/libyuv/include"
  )
  target_compile_definitions(talk_media PRIVATE
    TALK_WITH_WEBRTC
    JSON_NOEXCEPTION
    _HAS_EXCEPTIONS=0
  )
  target_compile_options(talk_media PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/utf-8 /std:c++20 /EHs-c- /GR /W3>
  )
  set_target_properties(talk_media PROPERTIES
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
  )
  set(TALK_HAS_MEDIA ON)
endif()

# 信令核心：/MT 独立 DLL，只导出 talk.h 的 C ABI。
set(TALK_CORE_SOURCES
  core/app/talk_client.cpp
  core/base/io_runtime.cpp
  core/c_api.cpp
  core/media/media_controller.cpp
  core/media/media_task_queue.cpp
  core/session/call_session.cpp
  core/signaling/signaling_client.cpp
)
if(NOT TALK_HAS_MEDIA)
  list(APPEND TALK_CORE_SOURCES core/media/null_media.cpp)
endif()

add_library(talk_core SHARED ${TALK_CORE_SOURCES})
target_include_directories(talk_core PUBLIC include PRIVATE core)
target_compile_definitions(talk_core PRIVATE
  TALK_BUILDING_LIBRARY
  LOGCPP_ACTIVE_LEVEL=LOGCPP_LEVEL_DEBUG
  ASIO_STANDALONE
  _WEBSOCKETPP_CPP11_STL_
  _WIN32_WINNT=0x0601
  WIN32_LEAN_AND_MEAN
  NOMINMAX
  _WINSOCK_DEPRECATED_NO_WARNINGS
)
target_compile_definitions(talk_core PRIVATE
  $<$<CONFIG:Debug>:_HAS_ITERATOR_DEBUGGING=0>
)
target_compile_options(talk_core PRIVATE
  $<$<CXX_COMPILER_ID:MSVC>:/utf-8 /std:c++20 /EHsc /GR /W3>
)
target_link_libraries(talk_core PRIVATE
  talk_protocol asio::asio log-cpp::log-cpp ws2_32 mswsock
)
if(TALK_HAS_MEDIA)
  target_link_libraries(talk_core PRIVATE talk_media)
endif()
target_include_directories(talk_core PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/third_party")
set_target_properties(talk_core PROPERTIES
  MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
)
# 核心 DLL 依赖 log-cpp.dll；放在同一输出目录，ctest 也能直接加载。
add_custom_command(TARGET talk_core POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          $<TARGET_FILE:log-cpp::log-cpp> $<TARGET_FILE_DIR:talk_core>)

# 命令行联调工具：/MT，只依赖 C API。
add_executable(talk-cli
  apps/talk-cli/main.cpp
  apps/common/log_setup.cpp
  apps/common/log_setup.h
)
target_include_directories(talk-cli PRIVATE apps/common)
target_compile_definitions(talk-cli PRIVATE
  LOGCPP_ACTIVE_LEVEL=LOGCPP_LEVEL_DEBUG
)
target_link_libraries(talk-cli PRIVATE talk_core talk_protocol log-cpp::log-cpp)
target_compile_options(talk-cli PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8 /std:c++20 /EHsc /GR /W3>)
set_target_properties(talk-cli PROPERTIES
  MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
)
add_custom_command(TARGET talk-cli POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          $<TARGET_FILE:talk_core> $<TARGET_FILE_DIR:talk-cli>)

# Qt 界面：/MD，只通过 C API 访问 core，不包含任何 WebRTC 头。
if(TALK_WITH_QT)
  find_package(Qt5 5.15 COMPONENTS Widgets Network REQUIRED)

  if(TALK_BUILD_LEGACY_QT_DEMO)
    add_executable(talk-qt
      apps/talk-qt/main.cpp
      apps/common/log_setup.cpp
      apps/common/log_setup.h
      ui/qt/app_settings.cpp
      ui/qt/app_settings.h
      ui/qt/core_client.cpp
      ui/qt/core_client.h
      ui/qt/video_view.cpp
      ui/qt/video_view.h
      ui/qt/window.cpp
      ui/qt/window.h
    )
    target_include_directories(talk-qt PRIVATE include ui/qt apps/common)
    target_compile_definitions(talk-qt PRIVATE
      LOGCPP_ACTIVE_LEVEL=LOGCPP_LEVEL_DEBUG
    )
    target_link_libraries(talk-qt PRIVATE
      talk_core log-cpp::log-cpp Qt5::Widgets Qt5::Network
    )
    target_compile_options(talk-qt PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8 /EHsc /GR /W3>)
    if(MSVC)
      configure_file(cmake/version.rc.in "${CMAKE_CURRENT_BINARY_DIR}/talk-qt.rc" @ONLY)
      target_sources(talk-qt PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/talk-qt.rc")
    endif()
    set_target_properties(talk-qt PROPERTIES
      CXX_STANDARD 17
      CXX_STANDARD_REQUIRED ON
      AUTOMOC ON
      WIN32_EXECUTABLE TRUE
      MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL"
    )
    add_custom_command(TARGET talk-qt POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              $<TARGET_FILE:talk_core> $<TARGET_FILE_DIR:talk-qt>)
  endif()

  if(TALK_BUILD_DESKTOP)
    add_subdirectory(ui/desktop)

    add_executable(talk-desktop
      apps/talk-desktop/main.cpp
      apps/common/log_setup.cpp
      apps/common/log_setup.h
    )
    target_sources(talk-desktop PRIVATE
      ui/desktop/resources/resources.qrc
    )
    target_include_directories(talk-desktop PRIVATE
      include
      apps/common
    )
    target_compile_definitions(talk-desktop PRIVATE
      LOGCPP_ACTIVE_LEVEL=LOGCPP_LEVEL_DEBUG
    )
    target_link_libraries(talk-desktop PRIVATE
      talk_desktop_ui
      talk_core
      log-cpp::log-cpp
      Qt5::Widgets
      Qt5::Network
    )
    target_compile_options(talk-desktop PRIVATE
      $<$<CXX_COMPILER_ID:MSVC>:/utf-8 /EHsc /GR /W3>
    )
    if(MSVC)
      configure_file(cmake/talk-desktop.rc.in
                     "${CMAKE_CURRENT_BINARY_DIR}/talk-desktop.rc" @ONLY)
      target_sources(talk-desktop PRIVATE
                     "${CMAKE_CURRENT_BINARY_DIR}/talk-desktop.rc")
    endif()
    set_target_properties(talk-desktop PROPERTIES
      CXX_STANDARD 17
      CXX_STANDARD_REQUIRED ON
      AUTOMOC ON
      AUTORCC ON
      WIN32_EXECUTABLE TRUE
      MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL"
    )
    add_custom_command(TARGET talk-desktop POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              $<TARGET_FILE:talk_core> $<TARGET_FILE_DIR:talk-desktop>)
  endif()
endif()
if(TALK_BUILD_TESTS)
  enable_testing()

  add_executable(api_test tests/api_test.c)
  target_link_libraries(api_test PRIVATE talk_core)
  target_compile_options(api_test PRIVATE $<$<C_COMPILER_ID:MSVC>:/utf-8 /W3>)
  set_target_properties(api_test PROPERTIES
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
  )
  add_test(NAME api_test COMMAND api_test)

  add_executable(protocol_test tests/protocol_test.cpp)
  target_link_libraries(protocol_test PRIVATE talk_protocol)
  target_compile_options(protocol_test PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8 /std:c++20 /EHsc /GR /W3>)
  set_target_properties(protocol_test PROPERTIES
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
  )
  add_test(NAME protocol_test COMMAND protocol_test)

  add_executable(signaling_test tests/signaling_test.cpp)
  target_include_directories(signaling_test PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/third_party"
  )
  target_compile_definitions(signaling_test PRIVATE
    ASIO_STANDALONE
    _WEBSOCKETPP_CPP11_STL_
    _WIN32_WINNT=0x0601
    WIN32_LEAN_AND_MEAN
    NOMINMAX
    _WINSOCK_DEPRECATED_NO_WARNINGS
  )
  target_compile_options(signaling_test PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8 /std:c++20 /EHsc /GR /W3>)
  target_link_libraries(signaling_test PRIVATE
    talk_core talk_protocol asio::asio ws2_32 mswsock
  )
  set_target_properties(signaling_test PROPERTIES
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
  )
  add_test(NAME signaling_test COMMAND signaling_test)
  if(TARGET talk_desktop_core)
    add_executable(desktop_core_test tests/desktop_core_test.cpp)
    target_link_libraries(desktop_core_test PRIVATE
      talk_desktop_core Qt5::Core
    )
    target_compile_options(desktop_core_test PRIVATE
      $<$<CXX_COMPILER_ID:MSVC>:/utf-8 /EHsc /GR /W3>
    )
    set_target_properties(desktop_core_test PROPERTIES
      CXX_STANDARD 17
      CXX_STANDARD_REQUIRED ON
      MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL"
    )
    add_test(NAME desktop_core_test COMMAND desktop_core_test)
  endif()

  # 视频编解码回环：直接调用 talk_media 的 MF 封装，不经过 WebRTC。
  if(TALK_HAS_MEDIA)
    add_executable(video_codec_test tests/video_codec_test.cpp)
    target_include_directories(video_codec_test PRIVATE
      "${CMAKE_CURRENT_SOURCE_DIR}/core"
    )
    # 与 talk_core 一致：链接 talk_webrtc 以获得 webrtc.lib 及其编译宏，
    # 否则 Debug 下 _ITERATOR_DEBUG_LEVEL 与 talk_media 不匹配。
    # 测试本身不抛异常（只用 printf + 返回值），_HAS_EXCEPTIONS=0 可接受。
    target_link_libraries(video_codec_test PRIVATE talk_media talk_webrtc)
    target_compile_options(video_codec_test PRIVATE
      $<$<CXX_COMPILER_ID:MSVC>:/utf-8 /std:c++20 /EHsc /GR /W3>
    )
    set_target_properties(video_codec_test PROPERTIES
      MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    )
    add_test(NAME video_codec_test COMMAND video_codec_test)

    # 摄像头采集冒烟：枚举真实设备并计数送帧，不依赖服务端。
    add_executable(camera_probe tests/camera_probe.cpp)
    target_include_directories(camera_probe PRIVATE
      "${CMAKE_CURRENT_SOURCE_DIR}/core"
    )
    target_link_libraries(camera_probe PRIVATE talk_media talk_webrtc)
    target_compile_options(camera_probe PRIVATE
      $<$<CXX_COMPILER_ID:MSVC>:/utf-8 /std:c++20 /EHsc /GR /W3>
    )
    set_target_properties(camera_probe PROPERTIES
      MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
    )
    add_test(NAME camera_probe COMMAND camera_probe)
  endif()
endif()
