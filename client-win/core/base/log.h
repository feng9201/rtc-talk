// File: core/base/log.h
// Brief: 核心模块日志宏；log-cpp 由宿主进程初始化，未初始化时丢弃。
#ifndef TALK_BASE_LOG_H_
#define TALK_BASE_LOG_H_

#include <w_log_cpp.h>

// module 必须是字符串字面量，便于按模块检索日志文件。
#define TALK_LOG_DEBUG(module, ...) LOG_DEBUG("[" module "] " __VA_ARGS__)
#define TALK_LOG_INFO(module, ...) LOG_INFO("[" module "] " __VA_ARGS__)
#define TALK_LOG_WARN(module, ...) LOG_WARN("[" module "] " __VA_ARGS__)
#define TALK_LOG_ERROR(module, ...) LOG_ERR("[" module "] " __VA_ARGS__)

#endif  // TALK_BASE_LOG_H_
