add_library(rtcwin_signaling STATIC
  include/rtcwin/signaling/json.h
  include/rtcwin/signaling/protocol.h
  include/rtcwin/signaling/signaling_client.h
  src/signaling_client.cpp
  src/log_compat.h
)

target_include_directories(rtcwin_signaling
  PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
    "${CMAKE_SOURCE_DIR}/third_party"
  PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

target_compile_definitions(rtcwin_signaling PUBLIC
  ASIO_STANDALONE
  _WEBSOCKETPP_CPP11_STL_
  _WIN32_WINNT=0x0601
  WIN32_LEAN_AND_MEAN
  NOMINMAX
  _WINSOCK_DEPRECATED_NO_WARNINGS
  $<$<CONFIG:Debug>:_HAS_ITERATOR_DEBUGGING=0>
)

target_compile_options(rtcwin_signaling
  PUBLIC
    $<$<CXX_COMPILER_ID:MSVC>:/wd4267>
  PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/utf-8 /EHsc /GR /W3>
)

target_link_libraries(rtcwin_signaling
  PUBLIC
    nlohmann_json::nlohmann_json
    asio::asio
  PRIVATE
    ws2_32
    mswsock
    Threads::Threads
)

set_target_properties(rtcwin_signaling PROPERTIES
  CXX_STANDARD 17
  CXX_STANDARD_REQUIRED ON
  MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
)