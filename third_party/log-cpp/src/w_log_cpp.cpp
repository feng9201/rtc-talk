#include "w_log_cpp.h"

#include <spdlog/async.h>
#include <spdlog/async_logger.h>
#include <spdlog/common.h>
#include <spdlog/sinks/rotating_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include <iostream>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <utility>
#include <vector>

#include "details/counting_dup_sink.h"

#ifdef LOGCPP_SUPPORT_SQLITE
#include "spdlog/sinks/sqlite_sink.h"
#endif

namespace w_log_cpp {
namespace {

const char *kDefaultName = "app";
const char *kPattern = "[%Y-%m-%d %H:%M:%S.%e] %^[%L][%s:%#,%t] %v%$";

spdlog::level::level_enum toSpd(Level level) {
    switch (level) {
        case Level::Trace:
            return spdlog::level::trace;
        case Level::Debug:
            return spdlog::level::debug;
        case Level::Info:
            return spdlog::level::info;
        case Level::Warn:
            return spdlog::level::warn;
        case Level::Err:
            return spdlog::level::err;
        case Level::Critical:
            return spdlog::level::critical;
        case Level::Off:
        default:
            return spdlog::level::off;
    }
}

Level minLevel(Level a, Level b) { return static_cast<int>(a) < static_cast<int>(b) ? a : b; }

struct LoggerSlot {
    std::shared_ptr<spdlog::async_logger> logger;
    std::shared_ptr<spdlog::sinks::stdout_color_sink_mt> console;
    std::shared_ptr<details::counting_dup_sink> file_wrapper;
    std::shared_ptr<spdlog::sinks::sink> file_inner;
    std::shared_ptr<spdlog::sinks::sink> sqlite;
    Level logger_level = Level::Info;
    Level console_level = Level::Info;
    Level file_level = Level::Info;
};

class Engine {
public:
    static Engine &instance() {
        static Engine engine;
        return engine;
    }

    bool setThreadPoolSize(std::size_t queue_size, std::size_t n_threads) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (pool_ready_) {
            return false;
        }
        queue_size_ = queue_size == 0 ? 8192 : queue_size;
        n_threads_ = n_threads == 0 ? 1 : n_threads;
        return true;
    }

    bool initNamed(const InitConfig &config) {
        std::lock_guard<std::mutex> lock(mutex_);
        const std::string name = config.name.empty() ? kDefaultName : config.name;
        if (slots_.count(name) != 0) {
            return false;
        }

        ensurePoolUnlocked(config);

        std::vector<spdlog::sink_ptr> sinks;
        auto slot = std::make_shared<LoggerSlot>();
        slot->console_level = config.console_level;
        slot->file_level = config.file_level;

        if (config.enable_console) {
            slot->console = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
            slot->console->set_level(toSpd(config.console_level));
            sinks.push_back(slot->console);
        }

        if (!config.file_path.empty()) {
            slot->file_inner = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
                config.file_path, config.max_file_size, config.max_file_num);
            slot->file_inner->set_level(spdlog::level::trace);
            auto wrapper = std::make_shared<details::counting_dup_sink>(
                slot->file_inner, std::chrono::milliseconds(config.file_dedup_ms));
            wrapper->set_level(toSpd(config.file_level));
            slot->file_wrapper = wrapper;
            sinks.push_back(wrapper);
        }

#ifdef LOGCPP_SUPPORT_SQLITE
        if (!config.sqlite_path.empty()) {
            auto sqlite = std::make_shared<spdlog::sinks::sqlite_sink_mt>(config.sqlite_path);
            sqlite->set_level(toSpd(config.file_level));
            slot->sqlite = sqlite;
            sinks.push_back(sqlite);
        }
#else
        if (!config.sqlite_path.empty()) {
            std::cerr << "log-cpp: sqlite requested but LOGCPP_SUPPORT_SQLITE is OFF\n";
            return false;
        }
#endif

        if (sinks.empty()) {
            std::cerr << "log-cpp: init has no sink (console/file/sqlite)\n";
            return false;
        }

        // 总门槛 = 已启用出口级别的最小值；忽略 InitConfig::level，避免挡住更低的 sink 级别
        slot->logger_level = effectiveGate(*slot);
        const Level gate = slot->logger_level;
        auto logger = std::make_shared<spdlog::async_logger>(name, sinks.begin(), sinks.end(), spdlog::thread_pool(),
                                                             toOverflow(overflow_));
        logger->set_level(toSpd(gate));
        logger->set_pattern(kPattern);
        logger->flush_on(spdlog::level::err);
        slot->logger = logger;
        spdlog::register_logger(logger);

        slots_[name] = slot;
        if (!default_name_set_) {
            default_name_ = name;
            default_name_set_ = true;
            default_logger_ = logger;
        }
        initialized_ = true;
        return true;
    }

    bool isInitialized() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return initialized_;
    }

    void shutdown() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!initialized_) {
            return;
        }
        for (auto &entry : slots_) {
            if (entry.second && entry.second->file_wrapper) {
                entry.second->file_wrapper->close_window();
            }
            if (entry.second && entry.second->logger) {
                entry.second->logger->flush();
            }
        }
        slots_.clear();
        default_logger_.reset();
        spdlog::drop_all();
        spdlog::shutdown();
        initialized_ = false;
        pool_ready_ = false;
        default_name_set_ = false;
        default_name_ = kDefaultName;
    }

    void setLevel(const std::string &name, Level level) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto slot = findUnlocked(name);
        if (!slot) {
            return;
        }
        slot->logger_level = level;
        slot->console_level = level;
        slot->file_level = level;
        applyLevelsUnlocked(*slot);
    }

    void setConsoleLevel(const std::string &name, Level level) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto slot = findUnlocked(name);
        if (!slot) {
            return;
        }
        slot->console_level = level;
        slot->logger_level = effectiveGate(*slot);
        applyLevelsUnlocked(*slot);
    }

    void setFileLevel(const std::string &name, Level level) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto slot = findUnlocked(name);
        if (!slot) {
            return;
        }
        slot->file_level = level;
        slot->logger_level = effectiveGate(*slot);
        applyLevelsUnlocked(*slot);
    }

    void setFileDedup(const std::string &name, int milliseconds) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto slot = findUnlocked(name);
        if (!slot || !slot->file_wrapper) {
            return;
        }
        slot->file_wrapper->set_window(std::chrono::milliseconds(milliseconds < 0 ? 0 : milliseconds));
    }

    void setFlushOn(Level level) {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto &entry : slots_) {
            if (entry.second && entry.second->logger) {
                entry.second->logger->flush_on(toSpd(level));
            }
        }
    }

    std::shared_ptr<spdlog::async_logger> acquireLogger(const char *logger_name) {
        std::lock_guard<std::mutex> lock(mutex_);
        std::shared_ptr<spdlog::async_logger> logger;
        if (!logger_name || logger_name[0] == '\0') {
            logger = default_logger_;
        } else if (default_name_set_ && default_name_ == logger_name) {
            logger = default_logger_;
        } else {
            auto slot = findUnlocked(logger_name);
            logger = slot ? slot->logger : nullptr;
        }
        if (!logger && !initialized_) {
            static std::once_flag warn_once;
            std::call_once(warn_once, [] { std::cerr << "log-cpp: log() called before init(), message dropped\n"; });
        }
        return logger;
    }

    bool shouldLog(const std::string &name, Level level) {
        auto logger = acquireLogger(name.empty() ? nullptr : name.c_str());
        return logger && logger->should_log(toSpd(level));
    }

    void writeLog(const std::shared_ptr<spdlog::async_logger> &logger, Level level, const char *file, int line,
                  const char *func, const char *message, std::size_t message_size) {
        if (!logger) {
            return;
        }
        logger->log(spdlog::source_loc{file, line, func}, toSpd(level),
                    spdlog::string_view_t(message, message_size));
    }

    const std::string &defaultName() const { return default_name_; }

private:
    spdlog::async_overflow_policy toOverflow(Overflow overflow) const {
        return overflow == Overflow::OverrunOldest ? spdlog::async_overflow_policy::overrun_oldest
                                                   : spdlog::async_overflow_policy::block;
    }

    void ensurePoolUnlocked(const InitConfig &config) {
        if (pool_ready_) {
            return;
        }
        overflow_ = config.overflow;
        const std::size_t queue = queue_size_ != 0 ? queue_size_ : (config.queue_size == 0 ? 8192 : config.queue_size);
        const std::size_t threads = n_threads_ != 0 ? n_threads_ : (config.n_threads == 0 ? 1 : config.n_threads);
        spdlog::init_thread_pool(queue, threads);
        queue_size_ = queue;
        n_threads_ = threads;
        pool_ready_ = true;
    }

    std::shared_ptr<LoggerSlot> findUnlocked(const std::string &name) {
        const std::string &key = name.empty() ? default_name_ : name;
        auto it = slots_.find(key);
        if (it == slots_.end()) {
            return nullptr;
        }
        return it->second;
    }

    std::shared_ptr<LoggerSlot> findUnlocked(const char *name) {
        if (!name || name[0] == '\0') {
            auto it = slots_.find(default_name_);
            return it == slots_.end() ? nullptr : it->second;
        }
        auto it = slots_.find(name);
        return it == slots_.end() ? nullptr : it->second;
    }

    // 总门槛只由已启用的出口决定，不再与 logger_level 取 min（后者由本函数结果回写）
    static Level effectiveGate(const LoggerSlot &slot) {
        bool any = false;
        Level gate = Level::Off;
        if (slot.console) {
            gate = any ? minLevel(gate, slot.console_level) : slot.console_level;
            any = true;
        }
        if (slot.file_wrapper || slot.sqlite) {
            gate = any ? minLevel(gate, slot.file_level) : slot.file_level;
            any = true;
        }
        return any ? gate : Level::Off;
    }

    void applyLevelsUnlocked(LoggerSlot &slot) {
        if (slot.console) {
            slot.console->set_level(toSpd(slot.console_level));
        }
        if (slot.file_wrapper) {
            slot.file_wrapper->set_level(toSpd(slot.file_level));
        }
        if (slot.sqlite) {
            slot.sqlite->set_level(toSpd(slot.file_level));
        }
        if (slot.logger) {
            slot.logger->set_level(toSpd(effectiveGate(slot)));
        }
    }

    mutable std::mutex mutex_;
    std::unordered_map<std::string, std::shared_ptr<LoggerSlot>> slots_;
    std::shared_ptr<spdlog::async_logger> default_logger_;
    std::string default_name_ = kDefaultName;
    bool default_name_set_ = false;
    bool initialized_ = false;
    bool pool_ready_ = false;
    std::size_t queue_size_ = 0;
    std::size_t n_threads_ = 0;
    Overflow overflow_ = Overflow::Block;
};

}  // namespace

bool setThreadPoolSize(std::size_t queue_size, std::size_t n_threads) {
    return Engine::instance().setThreadPoolSize(queue_size, n_threads);
}

bool init(const InitConfig &config) { return Engine::instance().initNamed(config); }

bool initNamed(const std::string &name, const InitConfig &config) {
    InitConfig copy = config;
    copy.name = name.empty() ? copy.name : name;
    return Engine::instance().initNamed(copy);
}

bool isInitialized() { return Engine::instance().isInitialized(); }

void shutdown() { Engine::instance().shutdown(); }

void setLevel(Level level) { Engine::instance().setLevel({}, level); }

void setLevel(const std::string &logger_name, Level level) { Engine::instance().setLevel(logger_name, level); }

void setConsoleLevel(Level level) { Engine::instance().setConsoleLevel({}, level); }

void setConsoleLevel(const std::string &logger_name, Level level) {
    Engine::instance().setConsoleLevel(logger_name, level);
}

void setFileLevel(Level level) { Engine::instance().setFileLevel({}, level); }

void setFileLevel(const std::string &logger_name, Level level) { Engine::instance().setFileLevel(logger_name, level); }

void setFileDedup(int milliseconds) { Engine::instance().setFileDedup({}, milliseconds); }

void setFileDedup(const std::string &logger_name, int milliseconds) {
    Engine::instance().setFileDedup(logger_name, milliseconds);
}

void setFlushOn(Level level) { Engine::instance().setFlushOn(level); }

bool shouldLog(Level level) { return Engine::instance().shouldLog({}, level); }

bool shouldLog(const std::string &logger_name, Level level) { return Engine::instance().shouldLog(logger_name, level); }

void logImpl(const char *logger_name, Level level, const char *file, int line, const char *func,
             const char *message, std::size_t message_size) {
    auto logger = Engine::instance().acquireLogger(logger_name);
    if (!logger || !logger->should_log(toSpd(level))) {
        return;
    }
    Engine::instance().writeLog(logger, level, file, line, func, message, message_size);
}

void logImpl(const char *logger_name, Level level, const char *file, int line, const char *func,
             const std::string &message) {
    logImpl(logger_name, level, file, line, func, message.data(), message.size());
}

namespace detail {

LoggerHandle acquireLogger(const char *logger_name) {
    LoggerHandle handle;
    handle.ptr = Engine::instance().acquireLogger(logger_name);
    return handle;
}

bool loggerEnabled(const LoggerHandle &handle, Level level) {
    if (!handle.ptr) {
        return false;
    }
    return std::static_pointer_cast<spdlog::async_logger>(handle.ptr)->should_log(toSpd(level));
}

void writeLog(const LoggerHandle &handle, Level level, const char *file, int line, const char *func,
              const char *message, std::size_t message_size) {
    auto logger = std::static_pointer_cast<spdlog::async_logger>(handle.ptr);
    Engine::instance().writeLog(logger, level, file, line, func, message, message_size);
}

}  // namespace detail

}  // namespace w_log_cpp
