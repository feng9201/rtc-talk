add_library(talk_desktop_core STATIC
  api/command_correlator.cpp
  api/command_correlator.h
  api/core_event.h
  api/event_mapper.cpp
  api/event_mapper.h
  api/talk_core_client.cpp
  api/talk_core_client.h
  domain/app_types.h
  services/app_settings_service.cpp
  services/app_settings_service.h
  services/session_controller.cpp
  services/session_controller.h
  state/session_store.cpp
  state/session_store.h
)

target_include_directories(talk_desktop_core PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}"
  "${CMAKE_SOURCE_DIR}/include"
)
target_link_libraries(talk_desktop_core
  PUBLIC Qt5::Core Qt5::Gui Qt5::Network
  PRIVATE talk_core
)
target_compile_options(talk_desktop_core PRIVATE
  $<$<CXX_COMPILER_ID:MSVC>:/utf-8 /EHsc /GR /W3>
)
set_target_properties(talk_desktop_core PROPERTIES
  CXX_STANDARD 17
  CXX_STANDARD_REQUIRED ON
  AUTOMOC ON
  MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL"
)

add_library(talk_desktop_ui STATIC
  base/frameless_window.cpp
  base/frameless_window.h
  dialogs/room_policy_dialog.cpp
  dialogs/room_policy_dialog.h
  domain/app_types.h
  main_window.cpp
  main_window.h
  pages/prejoin_page.cpp
  pages/prejoin_page.h
  pages/room_page.cpp
  pages/room_page.h
  theme/theme.cpp
  theme/theme.h
  widgets/control_button.cpp
  widgets/control_button.h
  widgets/avatar.cpp
  widgets/avatar.h
  widgets/icon_button.cpp
  widgets/icon_button.h
  widgets/member_row.cpp
  widgets/member_row.h
  widgets/pill.cpp
  widgets/pill.h
  widgets/segmented_control.cpp
  widgets/segmented_control.h
  widgets/toast.cpp
  widgets/toast.h
  widgets/window_controls.cpp
  widgets/window_controls.h
)

target_include_directories(talk_desktop_ui PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}"
)
target_link_libraries(talk_desktop_ui PUBLIC
  Qt5::Widgets
  talk_desktop_core
)
target_compile_options(talk_desktop_ui PRIVATE
  $<$<CXX_COMPILER_ID:MSVC>:/utf-8 /EHsc /GR /W3>
)
set_target_properties(talk_desktop_ui PROPERTIES
  CXX_STANDARD 17
  CXX_STANDARD_REQUIRED ON
  AUTOMOC ON
  MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL"
)
