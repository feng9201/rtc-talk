#ifndef LOGCPP_INC_WLOGCPP
#define LOGCPP_INC_WLOGCPP

#include <chrono>
#include <cstddef>
#include <iterator>
#include <memory>
#include <string>
#include <utility>

#include <spdlog/fmt/fmt.h>

#if defined(LOGCPP_SHARED_LIB)
#if defined(_WIN32)
#ifdef LOGCPP_EXPORTS
#define LOGCPP_API __declspec(dllexport)
#else
#define LOGCPP_API __declspec(dllimport)
#endif
#else
#define LOGCPP_API __attribute__((visibility("default")))
#endif
#else
#define LOGCPP_API
#endif

#define LOGCPP_LEVEL_TRACE 0
#define LOGCPP_LEVEL_DEBUG 1
#define LOGCPP_LEVEL_INFO 2
#define LOGCPP_LEVEL_WARN 3
#define LOGCPP_LEVEL_ERR 4
#define LOGCPP_LEVEL_CRITICAL 5
#define LOGCPP_LEVEL_OFF 6

#ifndef LOGCPP_ACTIVE_LEVEL
#ifdef NDEBUG
#define LOGCPP_ACTIVE_LEVEL LOGCPP_LEVEL_INFO
#else
#define LOGCPP_ACTIVE_LEVEL LOGCPP_LEVEL_DEBUG
#endif
#endif

#if (defined(__cplusplus) && __cplusplus >= 202002L) || (defined(_MSVC_LANG) && _MSVC_LANG >= 202002L)
#define LOGCPP_CXX20 1
#else
#define LOGCPP_CXX20 0
#endif

namespace w_log_cpp {

enum class Level {
    Trace = LOGCPP_LEVEL_TRACE,
    Debug = LOGCPP_LEVEL_DEBUG,
    Info = LOGCPP_LEVEL_INFO,
    Warn = LOGCPP_LEVEL_WARN,
    Err = LOGCPP_LEVEL_ERR,
    Critical = LOGCPP_LEVEL_CRITICAL,
    Off = LOGCPP_LEVEL_OFF
};

enum class Overflow {
    Block,
    OverrunOldest
};

struct InitConfig {
    std::string name = "app";
    bool enable_console = true;
    std::string file_path;
    std::string sqlite_path;
    std::size_t max_file_size = 10 * 1024 * 1024;
    std::size_t max_file_num = 5;
    Level level = Level::Debug;
    Level console_level = Level::Info;
    Level file_level = Level::Info;
    std::size_t queue_size = 8192;
    std::size_t n_threads = 1;
    int file_dedup_ms = 0;
    Overflow overflow = Overflow::Block;
};

LOGCPP_API bool setThreadPoolSize(std::size_t queue_size, std::size_t n_threads);
LOGCPP_API bool init(const InitConfig &config = InitConfig{});
LOGCPP_API bool initNamed(const std::string &name, const InitConfig &config);
LOGCPP_API bool isInitialized();
LOGCPP_API void shutdown();

LOGCPP_API void setLevel(Level level);
LOGCPP_API void setLevel(const std::string &logger_name, Level level);
LOGCPP_API void setConsoleLevel(Level level);
LOGCPP_API void setConsoleLevel(const std::string &logger_name, Level level);
LOGCPP_API void setFileLevel(Level level);
LOGCPP_API void setFileLevel(const std::string &logger_name, Level level);
LOGCPP_API void setFileDedup(int milliseconds);
LOGCPP_API void setFileDedup(const std::string &logger_name, int milliseconds);
LOGCPP_API void setFlushOn(Level level);

LOGCPP_API bool shouldLog(Level level);
LOGCPP_API bool shouldLog(const std::string &logger_name, Level level);

LOGCPP_API void logImpl(const char *logger_name, Level level, const char *file, int line, const char *func,
                        const char *message, std::size_t message_size);
LOGCPP_API void logImpl(const char *logger_name, Level level, const char *file, int line, const char *func,
                        const std::string &message);

namespace detail {

// 热路径：mutex 内只拷贝一次 shared_ptr，format / 入队在锁外
struct LoggerHandle {
    std::shared_ptr<void> ptr;
};

LOGCPP_API LoggerHandle acquireLogger(const char *logger_name);
LOGCPP_API bool loggerEnabled(const LoggerHandle &handle, Level level);
LOGCPP_API void writeLog(const LoggerHandle &handle, Level level, const char *file, int line, const char *func,
                         const char *message, std::size_t message_size);

#if LOGCPP_CXX20
template <typename... Args>
void appendFormat(fmt::basic_memory_buffer<char, 256> &buf, fmt::format_string<Args...> fmt, Args &&...args) {
    fmt::format_to(std::back_inserter(buf), fmt, std::forward<Args>(args)...);
}

template <typename... Args>
void appendFormat(fmt::basic_memory_buffer<char, 256> &buf, const char *fmt, Args &&...args) {
    fmt::format_to(std::back_inserter(buf), fmt::runtime(fmt), std::forward<Args>(args)...);
}
#else
template <typename... Args>
void appendFormat(fmt::basic_memory_buffer<char, 256> &buf, const char *fmt, const Args &...args) {
    fmt::format_to(std::back_inserter(buf), fmt, args...);
}
#endif

template <typename Fmt, typename... Args>
void logMessage(const char *logger_name, Level level, const char *file, int line, const char *func, Fmt &&fmt,
                Args &&...args) {
    const LoggerHandle handle = acquireLogger(logger_name);
    if (!loggerEnabled(handle, level)) {
        return;
    }
    try {
        fmt::basic_memory_buffer<char, 256> buf;
        appendFormat(buf, std::forward<Fmt>(fmt), std::forward<Args>(args)...);
        writeLog(handle, level, file, line, func, buf.data(), buf.size());
    } catch (const std::exception &) {
    }
}

}  // namespace detail

#if LOGCPP_CXX20
template <typename... Args>
void log(Level level, const char *file, int line, const char *func, fmt::format_string<Args...> fmt, Args &&...args) {
    detail::logMessage(nullptr, level, file, line, func, fmt, std::forward<Args>(args)...);
}

template <typename... Args>
void log(Level level, const char *file, int line, const char *func, const char *fmt, Args &&...args) {
    detail::logMessage(nullptr, level, file, line, func, fmt, std::forward<Args>(args)...);
}

template <typename... Args>
void log(const std::string &logger_name, Level level, const char *file, int line, const char *func,
         fmt::format_string<Args...> fmt, Args &&...args) {
    detail::logMessage(logger_name.c_str(), level, file, line, func, fmt, std::forward<Args>(args)...);
}

template <typename... Args>
void log(const std::string &logger_name, Level level, const char *file, int line, const char *func, const char *fmt,
         Args &&...args) {
    detail::logMessage(logger_name.c_str(), level, file, line, func, fmt, std::forward<Args>(args)...);
}
#else
template <typename... Args>
void log(Level level, const char *file, int line, const char *func, const char *fmt, const Args &...args) {
    detail::logMessage(nullptr, level, file, line, func, fmt, args...);
}

template <typename... Args>
void log(const std::string &logger_name, Level level, const char *file, int line, const char *func, const char *fmt,
         const Args &...args) {
    detail::logMessage(logger_name.c_str(), level, file, line, func, fmt, args...);
}
#endif

class LogScope {
public:
    LogScope(const char *file, int line, const char *func)
        : file_(file), line_(line), func_(func), start_(std::chrono::steady_clock::now()) {
        log(Level::Debug, file_, line_, func_, "enter {}", func_);
    }

    ~LogScope() {
        const auto ms =
            std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start_).count();
        log(Level::Debug, file_, line_, func_, "exit {}, cost {} ms", func_, ms);
    }

private:
    const char *file_;
    int line_;
    const char *func_;
    std::chrono::steady_clock::time_point start_;
};

}  // namespace w_log_cpp

#if LOGCPP_ACTIVE_LEVEL <= LOGCPP_LEVEL_TRACE
#define LOG_TRACE(...) w_log_cpp::log(w_log_cpp::Level::Trace, __FILE__, __LINE__, __FUNCTION__, __VA_ARGS__)
#define LOG_TRACE_TO(name, ...) \
    w_log_cpp::log((name), w_log_cpp::Level::Trace, __FILE__, __LINE__, __FUNCTION__, __VA_ARGS__)
#else
#define LOG_TRACE(...) ((void)0)
#define LOG_TRACE_TO(name, ...) ((void)0)
#endif

#if LOGCPP_ACTIVE_LEVEL <= LOGCPP_LEVEL_DEBUG
#define LOG_DEBUG(...) w_log_cpp::log(w_log_cpp::Level::Debug, __FILE__, __LINE__, __FUNCTION__, __VA_ARGS__)
#define LOG_DEBUG_TO(name, ...) \
    w_log_cpp::log((name), w_log_cpp::Level::Debug, __FILE__, __LINE__, __FUNCTION__, __VA_ARGS__)
#define LOG_SCOPE() w_log_cpp::LogScope _w_log_scope_##__LINE__(__FILE__, __LINE__, __FUNCTION__)
#else
#define LOG_DEBUG(...) ((void)0)
#define LOG_DEBUG_TO(name, ...) ((void)0)
#define LOG_SCOPE() ((void)0)
#endif

#if LOGCPP_ACTIVE_LEVEL <= LOGCPP_LEVEL_INFO
#define LOG_INFO(...) w_log_cpp::log(w_log_cpp::Level::Info, __FILE__, __LINE__, __FUNCTION__, __VA_ARGS__)
#define LOG_INFO_TO(name, ...) \
    w_log_cpp::log((name), w_log_cpp::Level::Info, __FILE__, __LINE__, __FUNCTION__, __VA_ARGS__)
#else
#define LOG_INFO(...) ((void)0)
#define LOG_INFO_TO(name, ...) ((void)0)
#endif

#if LOGCPP_ACTIVE_LEVEL <= LOGCPP_LEVEL_WARN
#define LOG_WARN(...) w_log_cpp::log(w_log_cpp::Level::Warn, __FILE__, __LINE__, __FUNCTION__, __VA_ARGS__)
#define LOG_WARN_TO(name, ...) \
    w_log_cpp::log((name), w_log_cpp::Level::Warn, __FILE__, __LINE__, __FUNCTION__, __VA_ARGS__)
#else
#define LOG_WARN(...) ((void)0)
#define LOG_WARN_TO(name, ...) ((void)0)
#endif

#if LOGCPP_ACTIVE_LEVEL <= LOGCPP_LEVEL_ERR
#define LOG_ERR(...) w_log_cpp::log(w_log_cpp::Level::Err, __FILE__, __LINE__, __FUNCTION__, __VA_ARGS__)
#define LOG_ERR_TO(name, ...) \
    w_log_cpp::log((name), w_log_cpp::Level::Err, __FILE__, __LINE__, __FUNCTION__, __VA_ARGS__)
#else
#define LOG_ERR(...) ((void)0)
#define LOG_ERR_TO(name, ...) ((void)0)
#endif

#endif  // LOGCPP_INC_WLOGCPP
