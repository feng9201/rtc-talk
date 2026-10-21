// File: apps/common/log_setup.h
// Brief: 宿主进程的日志初始化：确定落盘路径并按配置启动 log-cpp。
#ifndef TALK_APPS_COMMON_LOG_SETUP_H_
#define TALK_APPS_COMMON_LOG_SETUP_H_

#include <cstddef>
#include <string>

namespace talk {

// 日志参数；file 为空时使用 DefaultLogPath()。
struct LogOptions {
  std::string file;
  std::string level = "debug";
  std::string console_level = "info";
  bool console = false;
  std::size_t max_size = 10 * 1024 * 1024;
  int max_files = 5;
};

// 默认路径：%LOCALAPPDATA%\rtc-talk\logs\talk-client.log。
std::string DefaultLogPath();

// 进程内日志生命周期；构造即初始化，析构时刷新并关闭。
// 必须在创建 talk_core 之前构造，核心模块只写日志不做初始化。
class LoggingGuard {
 public:
  explicit LoggingGuard(const LogOptions& options);
  ~LoggingGuard();

  LoggingGuard(const LoggingGuard&) = delete;
  LoggingGuard& operator=(const LoggingGuard&) = delete;

  bool ok() const { return !path_.empty(); }
  const std::string& path() const { return path_; }

 private:
  std::string path_;
};

}  // namespace talk

#endif  // TALK_APPS_COMMON_LOG_SETUP_H_
