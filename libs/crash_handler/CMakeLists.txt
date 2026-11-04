add_library(rtcwin_breakpad INTERFACE)
set(breakpad_root "${CMAKE_SOURCE_DIR}/third_party/breakpad")
target_include_directories(rtcwin_breakpad SYSTEM INTERFACE "${breakpad_root}/include")
foreach(component exception_handler crash_generation_client common)
  target_link_libraries(rtcwin_breakpad INTERFACE
    "${breakpad_root}/lib/x64/$<IF:$<CONFIG:Debug>,Debug,Release>/${component}.lib")
endforeach()

# Breakpad's /MT C++ interface stays inside this DLL. The /MD UI uses only C ABI.
add_library(rtcwin_crash_handler SHARED src/crash_handler.cpp)
target_include_directories(rtcwin_crash_handler PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_compile_definitions(rtcwin_crash_handler PRIVATE
  RTCWIN_CRASH_HANDLER_EXPORTS WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE
  _WIN32_WINNT=0x0A00)
target_compile_options(rtcwin_crash_handler PRIVATE /utf-8 /EHsc /W3
  $<$<CONFIG:Release>:/Zi>)
target_link_options(rtcwin_crash_handler PRIVATE $<$<CONFIG:Release>:/DEBUG>)
target_link_libraries(rtcwin_crash_handler PRIVATE rtcwin_breakpad shell32 ole32)
set_target_properties(rtcwin_crash_handler PROPERTIES
  MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
