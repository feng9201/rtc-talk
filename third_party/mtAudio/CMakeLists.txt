cmake_minimum_required(VERSION 3.17)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    add_compile_options("$<$<C_COMPILER_ID:MSVC>:/utf-8>")
    add_compile_options("$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")
endif()

# ── 重采样功能开关（默认关闭）──
# 开启后解码输出统一重采样为 16000Hz / 单声道 / 16-bit
option(MT_AUDIO_ENABLE_RESAMPLE "Enable resampling to 16000Hz/mono/16bit" OFF)

# ── 依赖 ──
find_package(fdk-aac CONFIG REQUIRED)
find_package(Opus CONFIG REQUIRED)

# miniaudio 是 header-only 库
find_path(MINIAUDIO_INCLUDE_DIRS "miniaudio.h")

# ── G.711 C 源文件 ──
set(G711_SRCS
    ${CMAKE_CURRENT_SOURCE_DIR}/g711/g711.c
)

# ── mtAudio 源文件 ──
set(MTAUDIO_SRCS
    # 公共头文件
    ${CMAKE_CURRENT_SOURCE_DIR}/include/mt_audio_decoder.h

    # 内部头文件
    ${CMAKE_CURRENT_SOURCE_DIR}/src/decoder_internal.h

    # 工厂
    ${CMAKE_CURRENT_SOURCE_DIR}/src/decoder_factory.cpp

    # 解码器实现
    ${CMAKE_CURRENT_SOURCE_DIR}/src/aac_decoder.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/opus_decoder.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/g711_decoder.cpp

    # 重采样器
    ${CMAKE_CURRENT_SOURCE_DIR}/src/audio_resampler.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/audio_resampler.h

    # miniaudio 实现（单一定义点）
    ${CMAKE_CURRENT_SOURCE_DIR}/src/miniaudio_impl.cpp

    # G.711 编解码
    ${G711_SRCS}
    ${CMAKE_CURRENT_SOURCE_DIR}/g711/g711.h
)

# ── 库 ──
add_library(mtAudio STATIC ${MTAUDIO_SRCS})

# ── 编译定义 ──
target_compile_definitions(mtAudio
    PRIVATE
        MTAUDIO_LIBRARY
    PUBLIC
        $<$<BOOL:${MT_AUDIO_ENABLE_RESAMPLE}>:MT_AUDIO_ENABLE_RESAMPLE>
)

# ── 头文件目录 ──
target_include_directories(mtAudio
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/g711
        ${MINIAUDIO_INCLUDE_DIRS}
)

# ── 链接库 ──
target_link_libraries(mtAudio
    PUBLIC
        FDK-AAC::fdk-aac
        Opus::opus
)

# ── VS 源码分组 ──
foreach(fileItem ${MTAUDIO_SRCS})
    get_filename_component(PARENT_DIR "${fileItem}" DIRECTORY)
    string(REPLACE "${CMAKE_CURRENT_SOURCE_DIR}" "" GROUP "${PARENT_DIR}")
    string(REPLACE "/" "\\" GROUP "${GROUP}")
    source_group("${GROUP}" FILES "${fileItem}")
endforeach()
