cmake_minimum_required(VERSION 3.15)

project(
    log_cpp_test
)

if(LOGCPP_BUILD_TESTS)
    add_executable(
        ${PROJECT_NAME}
        main.cpp
    )

    target_link_libraries(
        ${PROJECT_NAME}
        PUBLIC
            log-cpp
    )

    add_executable(log_cpp_stress stress_main.cpp)
    target_link_libraries(log_cpp_stress PUBLIC log-cpp)
    if(UNIX)
        target_link_libraries(log_cpp_stress PUBLIC pthread)
    endif()

    add_executable(log_cpp_cxx20 cpp20_main.cpp)
    target_link_libraries(log_cpp_cxx20 PUBLIC log-cpp)
    set_target_properties(log_cpp_cxx20 PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON CXX_EXTENSIONS OFF)
    if(MSVC)
        target_compile_options(log_cpp_cxx20 PRIVATE /Zc:__cplusplus)
    endif()
endif()

if(LOGCPP_BUILD_QT_TEST)
    find_package(QT NAMES Qt6 Qt5 COMPONENTS Core REQUIRED)
    find_package(Qt${QT_VERSION_MAJOR} COMPONENTS Core REQUIRED)
    add_executable(log_cpp_qt qt_main.cpp)
    target_link_libraries(log_cpp_qt PUBLIC log-cpp Qt${QT_VERSION_MAJOR}::Core)
endif()
