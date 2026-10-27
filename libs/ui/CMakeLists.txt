set(RTCWIN_UI_SOURCES
  include/rtcwin/ui/base/frameless_window.h
  include/rtcwin/ui/main_window.h
  include/rtcwin/ui/pages/prejoin_page.h
  include/rtcwin/ui/pages/room_page.h
  include/rtcwin/ui/theme/theme.h
  include/rtcwin/ui/video/video_grid.h
  include/rtcwin/ui/video/video_surface.h
  include/rtcwin/ui/video/video_tile.h
  include/rtcwin/ui/widgets/control_button.h
  include/rtcwin/ui/widgets/member_row.h
  include/rtcwin/ui/widgets/pill.h
  include/rtcwin/ui/widgets/segmented_control.h
  include/rtcwin/ui/widgets/window_controls.h
  src/base/frameless_window.cpp
  src/pages/prejoin_page.cpp
  src/pages/room_page.cpp
  src/theme/theme.cpp
  src/video/video_grid.cpp
  src/video/video_surface.cpp
  src/video/video_tile.cpp
  src/widgets/control_button.cpp
  src/widgets/member_row.cpp
  src/widgets/pill.cpp
  src/widgets/segmented_control.cpp
  src/widgets/window_controls.cpp
  src/main_window.cpp
  resources/resources.qrc
)

add_library(rtcwin_ui STATIC ${RTCWIN_UI_SOURCES})

target_include_directories(rtcwin_ui PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

target_link_libraries(rtcwin_ui
  PUBLIC
    rtcwin_client_gateway
    Qt5::Core
    Qt5::Gui
    Qt5::Widgets
)

target_compile_options(rtcwin_ui PRIVATE
  $<$<CXX_COMPILER_ID:MSVC>:/utf-8 /EHsc /GR /W3>
)

set_target_properties(rtcwin_ui PROPERTIES
  CXX_STANDARD 17
  CXX_STANDARD_REQUIRED ON
  AUTOMOC ON
  AUTORCC ON
  MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL"
)
