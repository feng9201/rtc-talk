#include <chrono>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>

#include "w_log_cpp.h"

int main() {
    w_log_cpp::InitConfig cfg;
    cfg.file_path = "./log/app.log";
    cfg.max_file_size = 1024 * 1024 * 10;
    cfg.max_file_num = 3;
    cfg.enable_console = true;
    cfg.console_level = w_log_cpp::Level::Debug;
    cfg.file_level = w_log_cpp::Level::Info;
    cfg.file_dedup_ms = 200;

    if (!w_log_cpp::init(cfg)) {
        std::cerr << "init failed\n";
        return 1;
    }

    LOG_DEBUG("console only if file_level is Info: {}", 1);
    LOG_INFO("hello {}", "info");
    LOG_WARN("hello warn");
    LOG_ERR("open failed: disk full");
    for (int i = 0; i < 100; ++i) {
        LOG_ERR("open failed: disk full");
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    LOG_INFO("after burst");

    w_log_cpp::InitConfig sdk_cfg;
    sdk_cfg.file_path = "./log/sdk.log";
    sdk_cfg.enable_console = false;
    sdk_cfg.file_level = w_log_cpp::Level::Warn;
    if (!w_log_cpp::initNamed("sdk", sdk_cfg)) {
        std::cerr << "initNamed sdk failed\n";
        return 1;
    }
    LOG_INFO_TO("sdk", "this info is filtered on sdk logger");
    LOG_ERR_TO("sdk", "sdk error {}", 42);

    w_log_cpp::setConsoleLevel(w_log_cpp::Level::Info);
    LOG_DEBUG("hidden on console after setConsoleLevel(Info)");
    LOG_INFO("still visible on console and file");

    {
        LOG_SCOPE();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    w_log_cpp::shutdown();
    std::cout << "log-cpp demo finished\n";
    return 0;
}
