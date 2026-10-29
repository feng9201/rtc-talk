#include <atomic>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "w_log_cpp.h"

namespace {

struct BenchResult {
    const char *name;
    std::uint64_t total_logs;
    double produce_ms;
    double shutdown_ms;
};

BenchResult runCase(const char *name, int threads, int logs_per_thread, bool console, w_log_cpp::Overflow overflow) {
    w_log_cpp::shutdown();

    w_log_cpp::setThreadPoolSize(1 << 16, 2);

    w_log_cpp::InitConfig cfg;
    cfg.file_path = std::string("./log/stress_") + name + ".log";
    cfg.max_file_size = 64 * 1024 * 1024;
    cfg.max_file_num = 3;
    cfg.enable_console = console;
    cfg.console_level = w_log_cpp::Level::Warn;
    cfg.file_level = w_log_cpp::Level::Info;
    cfg.queue_size = 1 << 16;
    cfg.n_threads = 2;
    cfg.overflow = overflow;
    cfg.file_dedup_ms = 0;

    if (!w_log_cpp::init(cfg)) {
        std::cerr << "init failed for case " << name << "\n";
        return BenchResult{name, 0, 0, 0};
    }

    std::atomic<bool> start{false};
    std::vector<std::thread> workers;
    workers.reserve(static_cast<std::size_t>(threads));

    for (int t = 0; t < threads; ++t) {
        workers.emplace_back([&, t] {
            while (!start.load(std::memory_order_acquire)) {
            }
            for (int i = 0; i < logs_per_thread; ++i) {
                LOG_INFO("t={} i={} payload={}", t, i, i * 17);
            }
        });
    }

    const auto t0 = std::chrono::steady_clock::now();
    start.store(true, std::memory_order_release);
    for (auto &w : workers) {
        w.join();
    }
    const auto t1 = std::chrono::steady_clock::now();

    w_log_cpp::shutdown();
    const auto t2 = std::chrono::steady_clock::now();

    BenchResult r;
    r.name = name;
    r.total_logs = static_cast<std::uint64_t>(threads) * static_cast<std::uint64_t>(logs_per_thread);
    r.produce_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    r.shutdown_ms = std::chrono::duration<double, std::milli>(t2 - t1).count();
    return r;
}

void printResult(const BenchResult &r) {
    const double produce_sec = r.produce_ms / 1000.0;
    const double rate = produce_sec > 0.0 ? static_cast<double>(r.total_logs) / produce_sec : 0.0;
    std::cout << "[" << r.name << "]\n"
              << "  logs=" << r.total_logs << "\n"
              << "  produce_ms=" << r.produce_ms << "\n"
              << "  shutdown_ms=" << r.shutdown_ms << "\n"
              << "  produce_rate=" << static_cast<std::uint64_t>(rate) << " logs/s\n";
}

}  // namespace

int main() {
    const int threads = static_cast<int>(std::thread::hardware_concurrency() == 0 ? 8 : std::thread::hardware_concurrency());
    const int logs_per_thread = 200000;

    std::cout << "log-cpp stress: threads=" << threads << " logs_per_thread=" << logs_per_thread << "\n";

    // 过滤路径：级别不够，不应 format / 入队
    {
        w_log_cpp::shutdown();
        w_log_cpp::InitConfig cfg;
        cfg.file_path = "./log/stress_filtered.log";
        cfg.enable_console = false;
        cfg.file_level = w_log_cpp::Level::Err;
        cfg.overflow = w_log_cpp::Overflow::OverrunOldest;
        if (!w_log_cpp::init(cfg)) {
            std::cerr << "init filtered failed\n";
            return 1;
        }

        const auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < threads * logs_per_thread; ++i) {
            LOG_INFO("should be filtered {}", i);
        }
        const auto t1 = std::chrono::steady_clock::now();
        w_log_cpp::shutdown();
        const double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        const double rate = ms > 0.0 ? (static_cast<double>(threads) * logs_per_thread) / (ms / 1000.0) : 0.0;
        std::cout << "[filtered_info_when_file_err]\n"
                  << "  produce_ms=" << ms << "\n"
                  << "  produce_rate=" << static_cast<std::uint64_t>(rate) << " checks/s\n";
    }

    printResult(runCase("file_block", threads, logs_per_thread, false, w_log_cpp::Overflow::Block));
    printResult(runCase("file_overrun", threads, logs_per_thread, false, w_log_cpp::Overflow::OverrunOldest));

    std::cout << "stress done\n";
    return 0;
}
