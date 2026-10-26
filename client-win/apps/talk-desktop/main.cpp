#include "main_window.h"
#include "theme/theme.h"

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QDir>
#include <QFileInfo>
#include <QTimer>

#include <string>

#include "log_setup.h"

namespace {

QString NarrowToQString(const std::string& value) {
  return QString::fromLocal8Bit(value.c_str());
}

}  // namespace

int main(int argc, char** argv) {
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
  QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
  QApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
#endif

  QApplication app(argc, argv);
  app.setOrganizationName(QStringLiteral("rtc-talk"));
  app.setApplicationName(QStringLiteral("talk-desktop"));
  app.setApplicationVersion(QStringLiteral("0.2.0"));

  QCommandLineParser parser;
  parser.addHelpOption();
  parser.addVersionOption();
  parser.addOption(
      {"show-room-preview", "显示房间页生产骨架预览"});
  parser.addOption(
      {"size", "窗口尺寸，例如 1440x900", "size", "1440x900"});
  parser.addOption(
      {"screenshot", "启动后保存窗口截图并退出", "path"});
  parser.addOption(
      {"smoke-seconds", "指定秒数后自动退出", "seconds"});
  parser.addOption(
      {"log-file", "日志文件路径", "path"});
  parser.addOption(
      {"log-level", "文件日志级别", "level"});
  parser.addOption(
      {"log-console", "同时输出控制台日志"});
  parser.process(app);

  talk::LogOptions log_options;
  if (parser.isSet("log-file")) {
    log_options.file = parser.value("log-file").toLocal8Bit().toStdString();
  }
  if (parser.isSet("log-level")) {
    log_options.level = parser.value("log-level").toLocal8Bit().toStdString();
  }
  log_options.console = parser.isSet("log-console");
  talk::LoggingGuard logging(log_options);

  talk::desktop::Theme::apply(app);

  QSize initial_size(1440, 900);
  const QStringList size_parts =
      parser.value("size").split(QStringLiteral("x"), Qt::SkipEmptyParts);
  if (size_parts.size() == 2) {
    const int width = size_parts.at(0).toInt();
    const int height = size_parts.at(1).toInt();
    if (width > 0 && height > 0) initial_size = QSize(width, height);
  }

  talk::desktop::MainWindow window;
  if (logging.ok()) window.SetLogPath(NarrowToQString(logging.path()));
  window.resize(initial_size);
  window.show();

  if (parser.isSet("show-room-preview")) {
    QTimer::singleShot(0, &window, &talk::desktop::MainWindow::ShowRoomPreview);
  }

  const QString screenshot = parser.value("screenshot").trimmed();
  if (!screenshot.isEmpty()) {
    QTimer::singleShot(700, &window, [&app, &window, screenshot] {
      const QString path = QFileInfo(screenshot).absoluteFilePath();
      QDir().mkpath(QFileInfo(path).absolutePath());
      const bool saved = window.grab().save(path);
      app.exit(saved ? 0 : 3);
    });
  } else if (parser.isSet("smoke-seconds")) {
    bool ok = false;
    const int seconds = parser.value("smoke-seconds").toInt(&ok);
    if (!ok || seconds < 1 || seconds > 60) return 2;
    QTimer::singleShot(seconds * 1000, &app, &QCoreApplication::quit);
  }

  return app.exec();
}
