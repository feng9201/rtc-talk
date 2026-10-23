# libwebrtc Windows SDK 集成（/MT 静态库）。
# SDK 布局与开关以 D:/google/webrtc_sdk_hevc/usage.md 为准。
set(WEBRTC_SDK_DIR "D:/google/webrtc_sdk_hevc" CACHE PATH "libwebrtc SDK 根目录")
if(NOT EXISTS "${WEBRTC_SDK_DIR}/include/api/peer_connection_interface.h")
  message(FATAL_ERROR "WEBRTC_SDK_DIR 无效：${WEBRTC_SDK_DIR}")
endif()

# Debug 用 Debug 库，其余配置用 Release 库（均为 /MT 系列）。
set(WEBRTC_LIB_CONFIG "$<IF:$<CONFIG:Debug>,Debug,Release>")

add_library(talk_webrtc INTERFACE)

target_include_directories(talk_webrtc SYSTEM INTERFACE
  "${WEBRTC_SDK_DIR}/include"
  "${WEBRTC_SDK_DIR}/include/gen"
  "${WEBRTC_SDK_DIR}/include/third_party/abseil-cpp"
  "${WEBRTC_SDK_DIR}/include/third_party/protobuf/src"
  "${WEBRTC_SDK_DIR}/include/third_party/perfetto/include"
)

# 与 examples/peerconnection_smoke 的 48 项宏保持一致。
target_compile_definitions(talk_webrtc INTERFACE
  SDK_EXCLUDE_AV1
  _HAS_NODISCARD
  _CRT_NONSTDC_NO_WARNINGS
  _WINSOCK_DEPRECATED_NO_WARNINGS
  _HAS_EXCEPTIONS=0
  _LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_NONE
  __STD_C
  _CRT_RAND_S
  _CRT_SECURE_NO_DEPRECATE
  _SCL_SECURE_NO_DEPRECATE
  _ATL_NO_OPENGL
  _WINDOWS
  CERT_CHAIN_PARA_HAS_EXTRA_FIELDS
  PSAPI_VERSION=2
  WIN32
  _SECURE_ATL
  WINAPI_FAMILY=WINAPI_FAMILY_DESKTOP_APP
  USE_AURA=1
  WIN32_LEAN_AND_MEAN
  NOMINMAX
  _UNICODE
  UNICODE
  NTDDI_VERSION=NTDDI_WIN11_GE
  _WIN32_WINNT=0x0A00
  WINVER=0x0A00
  WEBRTC_ENABLE_PROTOBUF=1
  WEBRTC_STRICT_FIELD_TRIALS=0
  WEBRTC_INCLUDE_INTERNAL_AUDIO_DEVICE
  RTC_ENABLE_VP9
  RTC_ENABLE_H265
  WEBRTC_HAVE_SCTP
  WEBRTC_ENCODER_PSNR_STATS
  PROTOBUF_ENABLE_DEBUG_LOGGING_MAY_LEAK_PII=0
  WEBRTC_ENABLE_AVX2
  RTC_ENABLE_WIN_WGC
  WEBRTC_DEPRECATE_PLAN_B
  WEBRTC_NON_STATIC_TRACE_EVENT_HANDLERS=1
  WEBRTC_WIN
  ABSL_ALLOCATOR_NOTHROW=1
  _ENABLE_EXTENDED_ALIGNED_STORAGE
  ABSL_FLAGS_STRIP_NAMES=0
  CHROMIUM
  LIBYUV_DISABLE_NEON
  LIBYUV_DISABLE_SVE
  LIBYUV_DISABLE_SME
  LIBYUV_DISABLE_LSX
  LIBYUV_DISABLE_LASX
)

target_compile_definitions(talk_webrtc INTERFACE
  $<$<CONFIG:Debug>:_DEBUG;_HAS_ITERATOR_DEBUGGING=0;DYNAMIC_ANNOTATIONS_ENABLED=1>
  $<$<NOT:$<CONFIG:Debug>>:NDEBUG;NVALGRIND;DYNAMIC_ANNOTATIONS_ENABLED=0>
)

target_link_libraries(talk_webrtc INTERFACE
  "${WEBRTC_SDK_DIR}/lib/x64/${WEBRTC_LIB_CONFIG}/webrtc.lib"
  "${WEBRTC_SDK_DIR}/lib/x64/${WEBRTC_LIB_CONFIG}/clang_rt.builtins-x86_64.lib"
  advapi32 comdlg32 dbghelp dnsapi gdi32 msimg32 odbc32 odbccp32 oleaut32
  shell32 shlwapi user32 usp10 uuid version wininet winmm winspool ws2_32
  delayimp kernel32 ole32 crypt32 iphlpapi secur32 dmoguids wmcodecdspuuid
  amstrmid msdmo strmiids bcrypt d3d11 dxgi shcore dwmapi
  # Media Foundation：桌面采集（WGC/DXGI）与 MF H.264 编解码器。
  # mferror 只有错误码头文件，没有对应 lib。
  mfplat mfuuid mf
)
