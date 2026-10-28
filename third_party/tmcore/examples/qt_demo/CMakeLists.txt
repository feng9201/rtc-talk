# Qt QWidget 验证程序（可选）
# 需要 Qt5（如 C:\Qt\5.15.2\msvc2019_64）：
#   cmake -B build-qt -DCMAKE_PREFIX_PATH=C:/Qt/5.15.2/msvc2019_64
find_package(Qt5 COMPONENTS Widgets QUIET)
if(NOT Qt5_FOUND)
    message(STATUS "Qt5 not found, skipping tmcore_qt_demo")
    return()
endif()

set(CMAKE_AUTOMOC ON)

add_executable(tmcore_qt_demo
    main.cpp
    MainWidget.h
    MainWidget.cpp
)
target_link_libraries(tmcore_qt_demo PRIVATE Qt5::Widgets tmcore)

if(MSVC)
    target_compile_options(tmcore_qt_demo PRIVATE /utf-8)
endif()
