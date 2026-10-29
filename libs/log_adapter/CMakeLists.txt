add_library(rtcwin_log_adapter SHARED
  include/rtcwin/log_adapter/log_api.h
  src/log_api.cpp
)

target_include_directories(rtcwin_log_adapter
  PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_compile_definitions(rtcwin_log_adapter PRIVATE
  RTCWIN_LOG_ADAPTER_EXPORTS
)

target_compile_options(rtcwin_log_adapter PRIVATE
  $<$<CXX_COMPILER_ID:MSVC>:/utf-8 /EHsc /GR /W3>
)

target_link_libraries(rtcwin_log_adapter PRIVATE
  log-cpp::log-cpp
)

set_target_properties(rtcwin_log_adapter PROPERTIES
  CXX_STANDARD 17
  CXX_STANDARD_REQUIRED ON
  MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL"
)

add_custom_command(TARGET rtcwin_log_adapter POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          $<TARGET_RUNTIME_DLLS:rtcwin_log_adapter>
          $<TARGET_FILE_DIR:rtcwin_log_adapter>
  COMMAND_EXPAND_LISTS
)
