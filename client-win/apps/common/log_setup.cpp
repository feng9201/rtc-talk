// File: apps/common/log_setup.cpp
// Brief: 实现默认日志路径、目录创建与 log-cpp 初始化。
#include "log_setup.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cctype>
#include <string>

#include <w_log_cpp.h>

namespace talk {
namespace {

// log-cpp 的滚动文件 sink 要求父目录已存在。
bool EnsureDirectory(const std::string& directory) {
  if (directory.empty()) {
    return false;
  }
  for (std::size_t index = 0; index < directory.size(); ++index) {
    if (directory[index] != '\\' && directory[index] != '/') {
      continue;
    }
    if (index == 2 && directory[1] == ':') {
      continue;  // 盘符根目录必须已存在。
    }
    const std::string part = directory.substr(0, index);
    if (!part.empty()) {
      CreateDirectoryA(part.c_str(), nullptr);
    }
  }
  CreateDirectoryA(directory.c_str(), nullptr);
  const DWORD attributes = GetFileAttributesA(directory.c_str());
  return attributes != INVALID_FILE_ATTRIBUTES &&
         (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

bool EnsureParent(const std::string& file) {
  const size_t slash = file.find_last_of("\\/");
  return slash != std::string::npos &&
         EnsureDirectory(file.substr(0, slash));
}

std::string Env(const char* name) {
  char buffer[1024] = {};
  const DWORD length =
      GetEnvironmentVariableA(name, buffer, sizeof(buffer));
  if (length == 0 || length >= static_cast<DWORD>(sizeof(buffer))) {
    return {};
  }
  return std::string(buffer, length);
}

std::string Lower(std::string text) {
  for (char& ch : text) {
    ch = static_cast<char>(
        std::tolower(static_cast<unsigned char>(ch)));
  }
  return text;
}

w_log_cpp::Level ParseLevel(const std::string& text,
                            w_log_cpp::Level fallback) {
  const std::string value = Lower(text);
  if (value == "trace") {
    return w_log_cpp::Level::Trace;
  }
  if (value == "debug") {
    return w_log_cpp::Level::Debug;
  }
  if (value == "info") {
    return w_log_cpp::Level::Info;
  }
  if (value == "warn" || value == "warning") {
    return w_log_cpp::Level::Warn;
  }
  if (value == "error" || value == "err") {
    return w_log_cpp::Level::Err;
  }
  if (value == "off") {
    return w_log_cpp::Level::Off;
  }
  return fallback;
}

}  // namespace

std::string DefaultLogPath() {
  std::string base = Env("LOCALAPPDATA");
  if (base.empty()) {
    char temp[MAX_PATH + 1] = {};
    const DWORD length = GetTempPathA(MAX_PATH + 1, temp);
    if (length == 0 || length > MAX_PATH) {
      return {};
    }
    base.assign(temp, length);
  }
  if (base.empty()) {
    return {};
  }
  if (base.back() != '\\' && base.back() != '/') {
    base.push_back('\\');
  }
  return base + "rtc-talk\\logs\\talk-client.log";
}

LoggingGuard::LoggingGuard(const LogOptions& options) {
  std::string file = options.file;
  if (file.empty()) {
    file = Env("TALK_LOG_FILE");
  }
  if (file.empty()) {
    file = DefaultLogPath();
  }
  if (file.empty() || !EnsureParent(file)) {
    return;
  }

  std::string level = options.level;
  const std::string env_level = Env("TALK_LOG_LEVEL");
  if (!env_level.empty()) {
    level = env_level;
  }

  w_log_cpp::InitConfig config;
  config.name = "talk";
  config.enable_console = options.console;
  config.file_path = file;
  config.max_file_size =
      options.max_size == 0 ? 10 * 1024 * 1024 : options.max_size;
  config.max_file_num =
      options.max_files <= 0 ? 1 : static_cast<std::size_t>(options.max_files);
  config.level = w_log_cpp::Level::Trace;
  config.console_level =
      ParseLevel(options.console_level, w_log_cpp::Level::Info);
  config.file_level = ParseLevel(level, w_log_cpp::Level::Debug);
  // 文件日志不能反压业务线程；突发日志宁可丢弃最旧记录。
  config.overflow = w_log_cpp::Overflow::OverrunOldest;
  if (!w_log_cpp::init(config)) {
    return;
  }
  w_log_cpp::setFlushOn(w_log_cpp::Level::Warn);
  path_ = file;
  LOG_INFO("[app] logging started file={} level={} console={}", path_,
           level, options.console);
}

LoggingGuard::~LoggingGuard() {
  if (path_.empty()) {
    return;
  }
  LOG_INFO("[app] logging stopped");
  w_log_cpp::flush();
  w_log_cpp::shutdown();
  path_.clear();
}

}  // namespace talk
