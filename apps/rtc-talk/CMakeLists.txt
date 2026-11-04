include("${CMAKE_SOURCE_DIR}/cmake/rtc_talk_rc_setting.cmake")
add_executable(rtc-talk main.cpp "${WINDOWS_RC_FILE}")
target_include_directories(rtc-talk PRIVATE "${VERSION_PATH}")

target_link_libraries(rtc-talk PRIVATE rtcwin_ui rtcwin_crash_handler)
target_compile_definitions(rtc-talk PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
target_compile_options(rtc-talk PRIVATE $<$<CONFIG:Release>:/Zi>)
target_link_options(rtc-talk PRIVATE $<$<CONFIG:Release>:/DEBUG>)

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
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          $<TARGET_FILE:rtcwin_crash_handler>
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
