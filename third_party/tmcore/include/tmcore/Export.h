#pragma once
// tmcore 导出宏
//
// 使用方式：
//   - 默认（静态库）：CMake 向调用方传播 TMCORE_STATIC，TMCORE_API 为空
//   - 动态库（-DTMCORE_BUILD_SHARED=ON）：构建时定义 TMCORE_EXPORTS 导出符号，
//     调用方包含本头文件时自动 dllimport

#if defined(TMCORE_STATIC)
#  define TMCORE_API
#elif defined(TMCORE_EXPORTS)
#  if defined(_MSC_VER)
#    define TMCORE_API __declspec(dllexport)
#  else
#    define TMCORE_API __attribute__((visibility("default")))
#  endif
#else
#  if defined(_MSC_VER)
#    define TMCORE_API __declspec(dllimport)
#  else
#    define TMCORE_API __attribute__((visibility("default")))
#  endif
#endif
