cmake_minimum_required(VERSION 3.17)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    add_compile_options("$<$<C_COMPILER_ID:MSVC>:/utf-8>")
    add_compile_options("$<$<CXX_COMPILER_ID:MSVC>:/utf-8>")
endif()

# ── 依赖 ──
# mtAudio 提供 AudioFrame 结构（同一 CMake 项目内 target）

# miniaudio 是 header-only 库
find_path(MINIAUDIO_INCLUDE_DIRS "miniaudio.h")

# ── 源文件 ──
set(MTAUDIOPLAYER_SRCS
    # 公共头文件
    ${CMAKE_CURRENT_SOURCE_DIR}/include/mt_audio_player.h

    # 内部头文件
    ${CMAKE_CURRENT_SOURCE_DIR}/src/audio_player_internal.h

    # 实现
    ${CMAKE_CURRENT_SOURCE_DIR}/src/audio_player_impl.cpp
)

# ── 库 ──
add_library(mtAudioPlayer STATIC ${MTAUDIOPLAYER_SRCS})

# ── 头文件目录 ──
target_include_directories(mtAudioPlayer
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${MINIAUDIO_INCLUDE_DIRS}
)

# ── 链接库 ──
target_link_libraries(mtAudioPlayer
    PUBLIC
        mtAudio
)

# ── VS 源码分组 ──
foreach(fileItem ${MTAUDIOPLAYER_SRCS})
    get_filename_component(PARENT_DIR "${fileItem}" DIRECTORY)
    string(REPLACE "${CMAKE_CURRENT_SOURCE_DIR}" "" GROUP "${PARENT_DIR}")
    string(REPLACE "/" "\\" GROUP "${GROUP}")
    source_group("${GROUP}" FILES "${fileItem}")
endforeach()
