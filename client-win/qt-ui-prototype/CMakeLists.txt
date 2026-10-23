cmake_minimum_required(VERSION 3.16)

project(rtc_talk_qt_ui_prototype VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt5 5.15 REQUIRED COMPONENTS Core Gui Widgets)

file(GLOB_RECURSE APP_HEADERS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h")
file(GLOB_RECURSE APP_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
file(GLOB_RECURSE APP_RESOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/resources/*.qrc")

add_executable(rtc_talk_qt_ui WIN32 ${APP_HEADERS} ${APP_SOURCES} ${APP_RESOURCES})
target_include_directories(rtc_talk_qt_ui PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(rtc_talk_qt_ui PRIVATE Qt5::Core Qt5::Gui Qt5::Widgets)

if(WIN32)
    target_compile_options(rtc_talk_qt_ui PRIVATE /utf-8)
    target_link_libraries(rtc_talk_qt_ui PRIVATE user32 dwmapi)
endif()

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/src" FILES ${APP_HEADERS} ${APP_SOURCES})
source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}/resources" FILES ${APP_RESOURCES})

install(TARGETS rtc_talk_qt_ui RUNTIME DESTINATION bin)
