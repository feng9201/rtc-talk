cmake_minimum_required(VERSION 3.24)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/Version.cmake")
read_version("${CMAKE_CURRENT_SOURCE_DIR}/Version.txt")
project(rtc_talk_win VERSION "${CURRENT_VERSION}" LANGUAGES C CXX RC)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/Version.txt")
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/version_setting.cmake")

cmake_policy(SET CMP0091 NEW)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(RTCWIN_BUILD_TESTS "Build unit tests" ON)
option(RTCWIN_BUILD_MUXPLAYER "Build muxPlayer integration target" ON)
option(RTCWIN_BUILD_WEBRTC_MEDIA "Build libwebrtc media module" ON)

if(NOT WIN32)
  message(FATAL_ERROR "rtc-talk-win currently supports Windows only")
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
foreach(config Debug Release RelWithDebInfo MinSizeRel)
  string(TOUPPER "${config}" upper)
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_${upper}
      "${CMAKE_BINARY_DIR}/bin/${config}")
  set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY_${upper}
      "${CMAKE_BINARY_DIR}/lib/${config}")
  set(CMAKE_LIBRARY_OUTPUT_DIRECTORY_${upper}
      "${CMAKE_BINARY_DIR}/bin/${config}")
endforeach()

find_package(Qt5 5.15 REQUIRED COMPONENTS Core Gui Widgets Network OpenGL)
find_package(OpenGL REQUIRED)
find_package(asio CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(log-cpp CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)
find_package(Threads REQUIRED)

if(RTCWIN_BUILD_WEBRTC_MEDIA)
  include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/webrtc.cmake")
  add_subdirectory(libs/webrtc_media)
endif()

add_subdirectory(libs/domain)
add_subdirectory(libs/application)
add_subdirectory(libs/client_api)
add_subdirectory(libs/signaling)
add_subdirectory(libs/camera_win)
add_subdirectory(libs/log_adapter)
add_subdirectory(libs/crash_handler)

if(RTCWIN_BUILD_MUXPLAYER)
  find_package(FFMPEG REQUIRED)

  if(FFMPEG_INCLUDE_DIRS)
    list(GET FFMPEG_INCLUDE_DIRS 0 RTCWIN_FFMPEG_INCLUDE_DIR)
    get_filename_component(RTCWIN_FFMPEG_PREFIX
      "${RTCWIN_FFMPEG_INCLUDE_DIR}" DIRECTORY)
    set(RTCWIN_FFMPEG_RUNTIME_DIR "${RTCWIN_FFMPEG_PREFIX}/bin"
        CACHE PATH "FFmpeg runtime DLL directory")
  endif()

  add_subdirectory(third_party/muxplayer)
  add_subdirectory(libs/muxplayer_adapter)
endif()

add_subdirectory(libs/engine)
add_subdirectory(libs/client_gateway)
add_subdirectory(libs/ui)
add_subdirectory(apps/rtc-talk)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/library_rc_setting.cmake")
foreach(target rtcwin_engine rtcwin_log_adapter rtcwin_crash_handler)
  rtcwin_add_library_version(${target})
endforeach()
target_include_directories(rtcwin_engine PRIVATE "${VERSION_PATH}")

if(RTCWIN_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
