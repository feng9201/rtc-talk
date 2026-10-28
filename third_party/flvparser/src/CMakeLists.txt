cmake_minimum_required(VERSION 3.15)

project(flvParser LANGUAGES CXX)

find_package(mttool CONFIG REQUIRED)

#控制导出导入宏
add_definitions(-DFLVPARSER_LIB)

set(CMAKE_CXX_STANDARD 14)

if (BUILD_SHARED_LIBS)
    add_definitions(-DFLVPARSER_LIB)
    add_definitions(-DFLVPARSER_SHARED)
endif()

#设置包含主目录
set(CMAKE_INCLUDE_CURRENT_DIR ON)
#获取所有cpp文件
file(GLOB_RECURSE CPPSRC_DIR_LIST "*.cpp" )

if(BUILD_SHARED_LIBS)
    add_library(${PROJECT_NAME}
            SHARED
            ${CPPSRC_DIR_LIST}
    )
else()
    add_library(${PROJECT_NAME}
            STATIC
            ${CPPSRC_DIR_LIST}
    )
endif()

target_include_directories(${PROJECT_NAME}
            PUBLIC
            ../Include
            )

target_link_libraries(${PROJECT_NAME}
    PUBLIC
        mttool::mttool
        
)
