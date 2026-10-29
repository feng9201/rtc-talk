cmake_minimum_required(VERSION 3.15)
include(GNUInstallDirs)

project(
    log-cpp
    VERSION 1.0.0
    DESCRIPTION "A thin spdlog facade for C++"
)

if(NOT TARGET spdlog::spdlog_header_only)
    find_package(spdlog CONFIG REQUIRED)
endif()

set(SQLiteSrc "")
set(SQLiteLink "")
if(LOGCPP_SUPPORT_SQLITE)
    if(NOT TARGET SQLiteCpp)
        find_package(SQLiteCpp CONFIG REQUIRED)
    endif()
    set(SQLiteSrc
        ${CMAKE_CURRENT_SOURCE_DIR}/../inc/spdlog/sinks/sqlite_sink.h
        ${CMAKE_CURRENT_SOURCE_DIR}/../inc/spdlog/details/sqlite_helper.h
        details/sqlite_helper.cpp
    )
    set(SQLiteLink SQLiteCpp)
endif()

set(LOGCPP_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/../inc/w_log_cpp.h
    w_log_cpp.cpp
    details/counting_dup_sink.h
    ${SQLiteSrc}
)

if(LOGCPP_BUILD_SHARED)
    add_library(${PROJECT_NAME} SHARED ${LOGCPP_SOURCES})
else()
    add_library(${PROJECT_NAME} STATIC ${LOGCPP_SOURCES})
endif()

add_library(${PROJECT_NAME}::${PROJECT_NAME} ALIAS ${PROJECT_NAME})

if(LOGCPP_BUILD_SHARED)
    target_compile_definitions(${PROJECT_NAME}
        PRIVATE LOGCPP_EXPORTS
        PUBLIC LOGCPP_SHARED_LIB
    )
endif()

if(LOGCPP_SUPPORT_SQLITE)
    target_compile_definitions(${PROJECT_NAME} PUBLIC LOGCPP_SUPPORT_SQLITE)
endif()

target_include_directories(${PROJECT_NAME}
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../inc>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(${PROJECT_NAME}
    PUBLIC
    spdlog::spdlog_header_only
    ${SQLiteLink}
)

install(
    TARGETS ${PROJECT_NAME}
    EXPORT "${PROJECT_NAME}Targets"
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
)

install(
    DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/../inc/"
    DESTINATION include
)

install(
    EXPORT ${PROJECT_NAME}Targets
    FILE ${PROJECT_NAME}Targets.cmake
    NAMESPACE ${PROJECT_NAME}::
    DESTINATION lib/cmake/${PROJECT_NAME}
)

# Bake install-time options into Config.cmake (ON/OFF), not consumer cache.
foreach(_flag LOGCPP_SUPPORT_SQLITE LOGCPP_BUILD_SHARED)
    if(${_flag})
        set(${_flag} ON)
    else()
        set(${_flag} OFF)
    endif()
endforeach()

include(CMakePackageConfigHelpers)
configure_package_config_file(${CMAKE_CURRENT_SOURCE_DIR}/../cmake/Config.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}Config.cmake"
    INSTALL_DESTINATION "lib/cmake/${PROJECT_NAME}"
    NO_SET_AND_CHECK_MACRO
    NO_CHECK_REQUIRED_COMPONENTS_MACRO
)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}ConfigVersion.cmake"
    VERSION "${CMAKE_PROJECT_VERSION}"
    COMPATIBILITY AnyNewerVersion
)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}Config.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}ConfigVersion.cmake
    DESTINATION lib/cmake/${PROJECT_NAME}
)
