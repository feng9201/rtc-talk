// File: apps/talk-qt/main.cpp
// Brief: Qt 客户端入口；支持无连接界面冒烟、截图与日志落盘。
#include <QApplication>
#include <QCommandLineParser>
#include <QDebug>
#include <QScreen>
#include <QTimer>

#include <string>

#include "log_setup.h"
#include "window.h"

namespace {

constexpr int kScreenshotDelayMs = 500;

void ScheduleSmoke(QApplication* app, TalkWindow* window, int seconds,
                   const QString& screenshot) {
  QTimer::singleShot(kScreenshotDelayMs, window, [app, window, screenshot] {
    for (auto* screen : app->screens()) {
      qInfo() << "screen" << screen->name() << screen->geometry();
    }
    if (!screenshot.isEmpty() && !window->grab().save(screenshot)) {
      app->exit(3);
    }
  });
  QTimer::singleShot(seconds * 1000, app, &QApplication::quit);
}

}  // namespace

int main(int argc, char** argv) {
  QApplication app(argc, argv);
  app.setOrganizationName("rtc-talk");
  app.setApplicationName("talk-qt");

  QCommandLineParser parser;
  parser.addHelpOption();
  parser.addOption(
      {"smoke-seconds", "无连接界面冒烟测试后自动退出", "seconds"});
  parser.addOption({"screenshot", "冒烟测试时保存本窗口截图", "path"});
  parser.addOption({"backend", "本次启动覆盖音频后端", "name"});
  parser.addOption({"log-file", "日志文件路径，默认用户目录", "path"});
  parser.addOption({"log-level", "文件日志级别", "level"});
  parser.addOption({"log-console", "调试时同时输出到控制台"});
  parser.process(app);

  QString backend_override;
  if (parser.isSet("backend")) {
    backend_override = parser.value("backend");
    if (backend_override != "none" && backend_override != "fake" &&
        backend_override != "wasapi") {
      return 2;
    }
  }

  const auto narrow = [](const QString& text) {
    return text.toLocal8Bit().toStdString();
  };
  talk::LogOptions log;
  if (parser.isSet("log-file")) {
    log.file = narrow(parser.value("log-file"));
  }
  if (parser.isSet("log-level")) {
    log.level = narrow(parser.value("log-level"));
  }
  log.console = parser.isSet("log-console");
  // 先于窗口初始化，保证核心库创建过程也有日志。
  talk::LoggingGuard logging(log);
  if (!logging.ok()) {
    qWarning() << "日志初始化失败";
  }

  TalkWindow window;
  if (!backend_override.isEmpty()) {
    window.SetBackendOverride(backend_override);
  }
  if (logging.ok()) {
    window.SetLogPath(QString::fromLocal8Bit(logging.path().c_str()));
  }
  window.show();
  if (parser.isSet("smoke-seconds")) {
    bool ok = false;
    const int seconds = parser.value("smoke-seconds").toInt(&ok);
    if (!ok || seconds < 1 || seconds > 60) {
      return 2;
    }
    ScheduleSmoke(&app, &window, seconds, parser.value("screenshot"));
  }
  return app.exec();
}
