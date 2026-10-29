cmake_minimum_required(VERSION 3.15)

option(LOGCPP_BUILD_SHARED "Build shared library so A/B/C share one logger instance" ON)
option(LOGCPP_SUPPORT_SQLITE "Enable sqlite sink" OFF)
option(LOGCPP_BUILD_TESTS "Build tests" OFF)
option(LOGCPP_BUILD_QT_TEST "Build QString formatter smoke test (needs local Qt Core)" OFF)

project(
    log-cpp
    VERSION 1.0.0
    DESCRIPTION "A thin spdlog facade for C++"
)

if(MSVC)
    add_compile_options("$<$<C_COMPILER_ID:MSVC>:/utf-8>")
    add_compile_options("$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")
endif()

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_subdirectory(src)

if(LOGCPP_BUILD_TESTS OR LOGCPP_BUILD_QT_TEST)
    add_subdirectory(test)
endif()
