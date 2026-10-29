#pragma once
#include "w_log_cpp.h"
#include <spdlog/common.h>
#include <spdlog/details/null_mutex.h>
#include <spdlog/details/synchronous_factory.h>
#include <spdlog/pattern_formatter.h>
#include <spdlog/sinks/base_sink.h>

#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>

#include "spdlog/details/sqlite_helper.h"

using details_sqlite_helper = w_log_cpp::details::sqlite_helper;

namespace spdlog {
namespace sinks {
template <typename Mutex>
class LOGCPP_API sqlite_sink final : public base_sink<Mutex> {
   public:
    sqlite_sink(filename_t db_name);

   protected:
    void sink_it_(const details::log_msg &msg) override;
    void flush_() override;

   private:
    filename_t db_name_;
    std::shared_ptr<details_sqlite_helper> sqlite_helper_;
};

using sqlite_sink_mt = sqlite_sink<std::mutex>;
using sqlite_sink_st = sqlite_sink<details::null_mutex>;

namespace {
std::string timestamp_to_string(log_clock::time_point timestamp) {
    std::time_t now_time = std::chrono::system_clock::to_time_t(timestamp);

    std::ostringstream oss;
    oss << std::put_time(std::localtime(&now_time), "%Y-%m-%d %H:%M:%S");
    uint64_t t = timestamp.time_since_epoch().count();
    t %= 10000000;
    t /= 10000;
    oss << "." << std::setw(3) << std::setfill('0') << t;
    return oss.str();
}
}  // namespace
template <typename Mutex>
sqlite_sink<Mutex>::sqlite_sink(filename_t db_name)
    : db_name_(std::move(db_name)), sqlite_helper_(std::make_shared<details_sqlite_helper>()) {
    sqlite_helper_->open(db_name_);
}

template <typename Mutex>
void sqlite_sink<Mutex>::sink_it_(const details::log_msg &msg) {
    std::string message{};
    std::string info{};
    if (!msg.source.empty()) {
        info = std::string(spdlog::details::short_filename_formatter<spdlog::details::null_scoped_padder>::basename(
                   msg.source.filename)) +
               ":" + std::to_string(msg.source.line) + "," + std::to_string(msg.thread_id);
    }
    message = std::string(msg.payload.data(), msg.payload.size());
    sqlite_helper_->write(timestamp_to_string(msg.time), (int)msg.level, info, message);
}

template <typename Mutex>
void sqlite_sink<Mutex>::flush_() {
    sqlite_helper_->flush();
}

}  // namespace sinks

//
// factory functions
//

template <typename Factory = spdlog::synchronous_factory>
inline std::shared_ptr<logger> rotating_logger_mt(const std::string &logger_name) {
    return Factory::template create<sinks::sqlite_sink_mt>(logger_name);
}

template <typename Factory = spdlog::synchronous_factory>
inline std::shared_ptr<logger> rotating_logger_st(const std::string &logger_name) {
    return Factory::template create<sinks::sqlite_sink_st>(logger_name);
}

}  // namespace spdlog