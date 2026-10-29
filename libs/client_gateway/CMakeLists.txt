add_library(rtcwin_client_gateway STATIC
  include/rtcwin/client_gateway/c_abi_engine_client.h
  src/c_abi_engine_client.cpp
)

target_include_directories(rtcwin_client_gateway PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(rtcwin_client_gateway
  PUBLIC
    rtcwin_application
    rtcwin_client_api
    Qt5::Core
    Qt5::Network
  PRIVATE
    rtcwin_engine
)

target_compile_options(rtcwin_client_gateway PRIVATE
  $<$<CXX_COMPILER_ID:MSVC>:/utf-8 /EHsc /GR /W3>
)

set_target_properties(rtcwin_client_gateway PROPERTIES
  CXX_STANDARD 17
  CXX_STANDARD_REQUIRED ON
  AUTOMOC ON
  MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL"
)
