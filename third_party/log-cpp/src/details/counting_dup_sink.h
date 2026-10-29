#ifndef LOGCPP_SRC_DETAILS_COUNTING_DUP_SINK_H
#define LOGCPP_SRC_DETAILS_COUNTING_DUP_SINK_H

#include <spdlog/details/log_msg.h>
#include <spdlog/formatter.h>
#include <spdlog/pattern_formatter.h>
#include <spdlog/sinks/base_sink.h>
#include <spdlog/sinks/sink.h>

#include <chrono>
#include <memory>
#include <mutex>
#include <string>
#include <utility>

namespace w_log_cpp {
namespace details {

class counting_dup_sink : public spdlog::sinks::base_sink<std::mutex> {
public:
    counting_dup_sink(std::shared_ptr<spdlog::sinks::sink> inner, std::chrono::milliseconds window)
        : inner_(std::move(inner)), window_(window) {
        if (inner_) {
            inner_->set_level(spdlog::level::trace);
        }
    }

    void set_window(std::chrono::milliseconds window) {
        std::lock_guard<std::mutex> lock(this->mutex_);
        flush_repeat_unlocked();
        window_ = window;
    }

    std::chrono::milliseconds window() const { return window_; }

    void close_window() {
        std::lock_guard<std::mutex> lock(this->mutex_);
        flush_repeat_unlocked();
        if (inner_) {
            inner_->flush();
        }
    }

protected:
    void sink_it_(const spdlog::details::log_msg &msg) override {
        if (!inner_) {
            return;
        }
        if (window_.count() <= 0) {
            inner_->log(msg);
            return;
        }

        const std::string payload(msg.payload.data(), msg.payload.size());
        const auto now = std::chrono::steady_clock::now();

        if (count_ > 0 && msg.level == last_level_ && payload == last_payload_ && (now - window_start_) < window_) {
            ++count_;
            return;
        }

        flush_repeat_unlocked();
        inner_->log(msg);
        last_payload_ = payload;
        last_logger_name_.assign(msg.logger_name.data(), msg.logger_name.size());
        last_level_ = msg.level;
        last_loc_ = msg.source;
        count_ = 1;
        window_start_ = now;
    }

    void flush_() override {
        if (inner_) {
            inner_->flush();
        }
    }

    void set_pattern_(const std::string &pattern) override {
        auto formatter = std::unique_ptr<spdlog::formatter>(new spdlog::pattern_formatter(pattern));
        set_formatter_(std::move(formatter));
    }

    void set_formatter_(std::unique_ptr<spdlog::formatter> sink_formatter) override {
        if (inner_) {
            inner_->set_formatter(std::move(sink_formatter));
        }
    }

private:
    void flush_repeat_unlocked() {
        if (!inner_ || count_ <= 1) {
            count_ = 0;
            last_payload_.clear();
            return;
        }

        const std::string extra = last_payload_ + " (x" + std::to_string(count_) + ")";
        spdlog::details::log_msg repeat_msg(last_loc_, last_logger_name_, last_level_, extra);
        inner_->log(repeat_msg);
        count_ = 0;
        last_payload_.clear();
    }

    std::shared_ptr<spdlog::sinks::sink> inner_;
    std::chrono::milliseconds window_{0};
    std::string last_payload_;
    std::string last_logger_name_;
    spdlog::source_loc last_loc_{};
    spdlog::level::level_enum last_level_{spdlog::level::off};
    int count_{0};
    std::chrono::steady_clock::time_point window_start_{};
};

}  // namespace details
}  // namespace w_log_cpp

#endif
