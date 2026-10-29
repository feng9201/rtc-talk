#include "w_log_cpp.h"
#include "w_log_cpp/qt_formatter.h"

#include <QByteArray>
#include <QString>
#include <iostream>

int main() {
    w_log_cpp::InitConfig cfg;
    cfg.file_path = "./log/qt_smoke.log";
    cfg.enable_console = true;
    cfg.console_level = w_log_cpp::Level::Info;
    cfg.file_level = w_log_cpp::Level::Info;
    if (!w_log_cpp::init(cfg)) {
        std::cerr << "init failed\n";
        return 1;
    }

    const QString draft = QStringLiteral("draft-001");
    const QByteArray bytes = QByteArrayLiteral("bytes-ok");
    LOG_INFO("open draft {}", draft);
    LOG_INFO("payload {}", bytes);

    w_log_cpp::shutdown();
    std::cout << "qt smoke ok\n";
    return 0;
}
