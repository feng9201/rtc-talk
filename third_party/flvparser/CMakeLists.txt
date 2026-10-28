cmake_minimum_required(VERSION 3.15)

project(flvParser)

add_subdirectory(src)

option(FLVPARSER_BUILD_TEST "Build test" OFF)
option(FLVPARSER_BUILD_STATIC "Build static library" ON)
option(FLVPARSER_BUILD_SHARED "Build shared library" OFF)

message("FLVPARSER_BUILD_TEST=${FLVPARSER_BUILD_TEST}")
if(${FLVPARSER_BUILD_TEST})
    add_subdirectory(test)
endif()