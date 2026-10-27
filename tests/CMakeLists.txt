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
