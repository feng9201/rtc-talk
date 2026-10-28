cmake_minimum_required(VERSION 3.16)
project(tmcore VERSION 1.0.0 LANGUAGES CXX)

option(TMCORE_BUILD_SHARED "Build tmcore as a shared library (default OFF: static library)" OFF)
option(TMCORE_BUILD_EXAMPLES "Build the example programs" ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(TMCORE_BUILD_SHARED)
    add_library(tmcore SHARED "")
    target_compile_definitions(tmcore PRIVATE TMCORE_EXPORTS)
else()
    # 默认：静态库，并向调用方传播 TMCORE_STATIC，使 TMCORE_API 为空
    add_library(tmcore STATIC "")
    target_compile_definitions(tmcore PUBLIC TMCORE_STATIC)
endif()

set_target_properties(tmcore PROPERTIES
    OUTPUT_NAME tmcore
    POSITION_INDEPENDENT_CODE ON
)

target_sources(tmcore PRIVATE
    src/NetworkMonitor.cpp
    src/AdapterCommon.cpp
    src/CpuUsage.cpp
    src/MemoryUsage.cpp
    src/GpuUsage.cpp
    src/GpuInfo.cpp
    src/GpuMemoryUsage.cpp
    src/D3D11Utils.cpp
    src/CpuFreq.cpp
    src/DiskUsage.cpp
    src/StringHelper.cpp
    src/pdh/PdhQuery.cpp
    src/pdh/PdhCpuUsage.cpp
    src/pdh/PdhGpuUsage.cpp
    src/pdh/PdhAdapterMemory.cpp
    src/pdh/PdhCpuFreq.cpp
    src/pdh/PdhDiskUsage.cpp
)

target_include_directories(tmcore
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(tmcore
    PUBLIC
        iphlpapi   # GetIfTable / GetAdaptersInfo / MIB_IFROW
        Pdh        # 性能计数器（CPU/显卡/硬盘利用率、CPU频率）
        powrprof   # CallNtPowerInformation（CPU最大频率）
    PRIVATE
        dxgi       # CreateDXGIFactory1（显卡枚举）
        d3d11      # D3D11CreateDevice（指定显卡创建设备）
)

target_compile_definitions(tmcore PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)

if(MSVC)
    # 源文件统一为 UTF-8 编码
    target_compile_options(tmcore PRIVATE /utf-8 /W3)
endif()

# ----------------------------------------------------------------------------
# 安装规则（可选使用）
# ----------------------------------------------------------------------------
include(GNUInstallDirs)
install(TARGETS tmcore
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/include/tmcore
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    FILES_MATCHING PATTERN "*.h"
)

# ----------------------------------------------------------------------------
# 示例程序
# ----------------------------------------------------------------------------
if(TMCORE_BUILD_EXAMPLES)
    add_subdirectory(examples/console_demo)
    add_subdirectory(examples/qt_demo)
endif()
