# SDK inputs, never another business project's build output.
set(WEBRTC_SOURCE_DIR "$ENV{WEBRTC_SOURCE_DIR}" CACHE PATH "Matching M146 WebRTC source")
set(WEBRTC_BUILD_DIR "$ENV{WEBRTC_BUILD_DIR}" CACHE PATH "Approved ARM64 WebRTC SDK output")
if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux" OR NOT CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64)$")
  message(FATAL_ERROR "This qualified WebRTC SDK integration currently targets RK ARM64 only")
endif()
foreach(path "${WEBRTC_SOURCE_DIR}/api/peer_connection_interface.h"
             "${WEBRTC_BUILD_DIR}/obj/libwebrtc.a" "${WEBRTC_BUILD_DIR}/args.gn")
  if(NOT EXISTS "${path}")
    message(FATAL_ERROR "Missing WebRTC SDK input: ${path}")
  endif()
endforeach()
file(READ "${WEBRTC_BUILD_DIR}/args.gn" _rtc_webrtc_args)
foreach(setting "use_thin_lto = false" "use_custom_libcxx = false" "use_elf_crel = false"
                "target_cpu = \"arm64\"" "rtc_use_h265 = true")
  string(FIND "${_rtc_webrtc_args}" "${setting}" _rtc_found)
  if(_rtc_found EQUAL -1)
    message(FATAL_ERROR "SDK ABI mismatch: requires ${setting}")
  endif()
endforeach()
add_library(rtc_webrtc INTERFACE)
target_include_directories(rtc_webrtc SYSTEM INTERFACE "${WEBRTC_SOURCE_DIR}"
  "${WEBRTC_SOURCE_DIR}/third_party/abseil-cpp" "${WEBRTC_SOURCE_DIR}/third_party/libyuv/include")
target_compile_definitions(rtc_webrtc INTERFACE WEBRTC_POSIX WEBRTC_LINUX WEBRTC_ARCH_ARM64
  WEBRTC_HAS_NEON RTC_ENABLE_H265 WEBRTC_INCLUDE_INTERNAL_AUDIO_DEVICE RTC_DISABLE_TRACE_EVENTS
  WEBRTC_ENABLE_PROTOBUF=0 WEBRTC_EXCLUDE_BUILT_IN_SSL_ROOT_CERTS WEBRTC_STRICT_FIELD_TRIALS=0
  WEBRTC_NON_STATIC_TRACE_EVENT_HANDLERS=0 WEBRTC_DEPRECATE_PLAN_B DYNAMIC_ANNOTATIONS_ENABLED=0 NDEBUG)
target_link_libraries(rtc_webrtc INTERFACE "${WEBRTC_BUILD_DIR}/obj/libwebrtc.a" Threads::Threads dl m)
# Match the existing RK integration: discard unreferenced SDK sections. This
# does not permit unresolved symbols in retained code or supply dummy routines.
target_link_options(rtc_webrtc INTERFACE "LINKER:--gc-sections")
