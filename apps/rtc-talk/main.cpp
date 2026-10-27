#include "rtcwin/ui/main_window.h"
#include "rtcwin/ui/theme/theme.h"

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QDir>
#include <QFileInfo>
#include <QSurfaceFormat>
#include <QTimer>

int main(int argc, char** argv) {
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
  QApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
  QApplication::setAttribute(Qt::AA_UseHighDpiPixmaps);
#endif
  QSurfaceFormat format;
  format.setSwapInterval(1);
  format.setColorSpace(QSurfaceFormat::sRGBColorSpace);
  QSurfaceFormat::setDefaultFormat(format);

  QApplication app(argc, argv);
  app.setOrganizationName(QStringLiteral("rtc-talk"));
  app.setApplicationName(QStringLiteral("rtc-talk-win"));
  app.setApplicationVersion(QStringLiteral("0.1.0"));

  QCommandLineParser parser;
  parser.addHelpOption();
  parser.addVersionOption();
  parser.addOption(QCommandLineOption(
      QStringLiteral("room-preview"), QStringLiteral("显示房间页预览")));
  parser.addOption(QCommandLineOption(
      QStringLiteral("size"), QStringLiteral("窗口尺寸"),
      QStringLiteral("size"), QStringLiteral("1440x900")));
  parser.addOption(QCommandLineOption(
      QStringLiteral("screenshot"), QStringLiteral("保存窗口截图后退出"),
      QStringLiteral("path")));
  parser.addOption(QCommandLineOption(
      QStringLiteral("screenshot-delay-ms"),
      QStringLiteral("截图延迟毫秒"), QStringLiteral("ms"),
      QStringLiteral("700")));
  parser.addOption(QCommandLineOption(
      QStringLiteral("smoke-seconds"), QStringLiteral("自动退出秒数"),
      QStringLiteral("seconds")));
  parser.process(app);

  rtcwin::ui::theme::Apply(app);

  QSize size(1440, 900);
  const QStringList parts =
      parser.value(QStringLiteral("size")).split(QLatin1Char('x'));
  if (parts.size() == 2) {
    const int width = parts.at(0).toInt();
    const int height = parts.at(1).toInt();
    if (width > 0 && height > 0) size = QSize(width, height);
  }

  rtcwin::ui::MainWindow window;
  window.resize(size);
  window.show();

  if (parser.isSet(QStringLiteral("room-preview"))) {
    QTimer::singleShot(0, &window,
                       &rtcwin::ui::MainWindow::ShowRoomPreview);
  }

  const QString screenshot = parser.value(QStringLiteral("screenshot"));
  if (!screenshot.trimmed().isEmpty()) {
    bool ok = false;
    const int delay =
        parser.value(QStringLiteral("screenshot-delay-ms")).toInt(&ok);
    QTimer::singleShot(ok && delay >= 0 ? delay : 700, &window,
                       [&app, &window, screenshot] {
      const QString path = QFileInfo(screenshot).absoluteFilePath();
      QDir().mkpath(QFileInfo(path).absolutePath());
      const bool saved = window.grab().save(path);
      app.exit(saved ? 0 : 3);
    });
  } else if (parser.isSet(QStringLiteral("smoke-seconds"))) {
    bool ok = false;
    const int seconds =
        parser.value(QStringLiteral("smoke-seconds")).toInt(&ok);
    if (!ok || seconds < 1 || seconds > 60) return 2;
    QTimer::singleShot(seconds * 1000, &app, &QCoreApplication::quit);
  }

  return app.exec();
}
