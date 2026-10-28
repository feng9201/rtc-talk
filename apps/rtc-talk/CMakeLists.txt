add_executable(rtc-talk main.cpp)

target_link_libraries(rtc-talk PRIVATE rtcwin_ui)

target_compile_options(rtc-talk PRIVATE
  $<$<CXX_COMPILER_ID:MSVC>:/utf-8 /EHsc /GR /W3>
)

set_target_properties(rtc-talk PROPERTIES
  CXX_STANDARD 17
  CXX_STANDARD_REQUIRED ON
  WIN32_EXECUTABLE TRUE
  MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL"
)

add_custom_command(TARGET rtc-talk POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          $<TARGET_FILE:rtcwin_engine>
          $<TARGET_FILE_DIR:rtc-talk>
)
if(RTCWIN_BUILD_MUXPLAYER AND
   EXISTS "${RTCWIN_FFMPEG_RUNTIME_DIR}/avutil-59.dll")
  add_custom_command(TARGET rtc-talk POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${RTCWIN_FFMPEG_RUNTIME_DIR}/avutil-59.dll"
            "$<TARGET_FILE_DIR:rtc-talk>/avutil-59.dll"
  )
endif()