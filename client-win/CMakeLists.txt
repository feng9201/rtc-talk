cmake_minimum_required(VERSION 3.24)
project(rtc_talk_client_win VERSION 0.1.0 LANGUAGES C CXX)

cmake_policy(SET CMP0091 NEW) # MSVC_RUNTIME_LIBRARY 按 target 设置

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(TALK_WITH_WEBRTC "构建 libwebrtc 媒体引擎（/MT）" ON)
option(TALK_WITH_QT "构建 Qt5 界面（/MD，仅走 C API）" ON)
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

# 协议类型与 JSON 工具（仅头文件，CRT 无关）。
add_library(talk_protocol INTERFACE)
target_include_directories(talk_protocol INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/core")
target_link_libraries(talk_protocol INTERFACE nlohmann_json::nlohmann_json)

# 媒体实现：/MT + 关闭异常，与 libwebrtc 一致；不向 core 传播 webrtc 编译宏。
set(TALK_HAS_MEDIA OFF)
if(TALK_WITH_WEBRTC)
  include(cmake/webrtc.cmake)
  add_library(talk_media STATIC core/media/webrtc_media.cpp)
  target_link_libraries(talk_media PRIVATE talk_protocol talk_webrtc)
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
set(TALK_CORE_SOURCES core/core.cpp)
if(NOT TALK_HAS_MEDIA)
  list(APPEND TALK_CORE_SOURCES core/media/null_media.cpp)
endif()

add_library(talk_core SHARED ${TALK_CORE_SOURCES})
target_include_directories(talk_core PUBLIC include PRIVATE core)
target_compile_definitions(talk_core PRIVATE
  TALK_BUILDING_LIBRARY
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
target_link_libraries(talk_core PRIVATE talk_protocol asio::asio ws2_32 mswsock)
if(TALK_HAS_MEDIA)
  target_link_libraries(talk_core PRIVATE talk_media)
endif()
target_include_directories(talk_core PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/third_party")
set_target_properties(talk_core PROPERTIES
  MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
)

# 命令行联调工具：/MT，只依赖 C API。
add_executable(talk-cli apps/talk-cli/main.cpp)
target_link_libraries(talk-cli PRIVATE talk_core talk_protocol)
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
  add_executable(talk-qt
    apps/talk-qt/main.cpp
    ui/qt/window.cpp
    ui/qt/window.h
  )
  target_include_directories(talk-qt PRIVATE include ui/qt)
  target_link_libraries(talk-qt PRIVATE talk_core Qt5::Widgets Qt5::Network)
  target_compile_options(talk-qt PRIVATE $<$<CXX_COMPILER_ID:MSVC>:/utf-8 /EHsc /GR /W3>)
  set_target_properties(talk-qt PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    AUTOMOC ON
    MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL"
  )
  add_custom_command(TARGET talk-qt POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:talk_core> $<TARGET_FILE_DIR:talk-qt>)
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
endif()
