cmake_minimum_required(VERSION 3.15)

project(test LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 11)

find_package(httplib CONFIG REQUIRED)

add_executable(test main.cpp)

target_link_libraries(test PRIVATE
        flvParser
        httplib::httplib
        )