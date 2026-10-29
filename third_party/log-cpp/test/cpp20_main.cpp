#include "w_log_cpp.h"

#include <iostream>
#include <string>

int main() {
    w_log_cpp::InitConfig cfg;
    cfg.file_path = "./log/cpp20_smoke.log";
    cfg.enable_console = false;
    cfg.file_level = w_log_cpp::Level::Info;
    if (!w_log_cpp::init(cfg)) {
        std::cerr << "init failed\n";
        return 1;
    }

    LOG_INFO("cxx20 literal {}", 42);

    const char *runtime_fmt = "runtime {}";
    LOG_INFO(runtime_fmt, "ok");

    {
        LOG_SCOPE();
    }

    w_log_cpp::shutdown();
    std::cout << "cpp20 smoke ok\n";
    return 0;
}
