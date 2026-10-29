#include "rtcwin/ui/main_window.h"
#include "rtcwin/ui/theme/theme.h"

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QDir>
#include <QFileInfo>
#include <QPixmap>
#include <QScreen>
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
  parser.addOption(QCommandLineOption(
      QStringLiteral("auto-preview"),
      QStringLiteral("设备枚举完成后自动启动 muxPlayer 本地预览")));
  parser.addOption(QCommandLineOption(
      QStringLiteral("auto-join-url"), QStringLiteral("联调信令地址"),
      QStringLiteral("url")));
  parser.addOption(QCommandLineOption(
      QStringLiteral("auto-join-room"), QStringLiteral("联调房间 ID"),
      QStringLiteral("room")));
  parser.addOption(QCommandLineOption(
      QStringLiteral("auto-join-uid"), QStringLiteral("联调用户 ID"),
      QStringLiteral("uid")));
  parser.addOption(QCommandLineOption(
      QStringLiteral("auto-join-dev-token"),
      QStringLiteral("联调用 /dev/token 获取开发凭证")));
  parser.addOption(QCommandLineOption(
      QStringLiteral("auto-leave-ms"), QStringLiteral("入房后自动离房间隔"),
      QStringLiteral("ms")));
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
  } else if (parser.isSet(QStringLiteral("auto-preview"))) {
    window.EnableCameraPreview();
  }

  if (parser.isSet(QStringLiteral("auto-join-url")) &&
      parser.isSet(QStringLiteral("auto-join-room")) &&
      parser.isSet(QStringLiteral("auto-join-uid"))) {
    rtcwin::domain::PrejoinOptions options;
    options.signaling_url =
        parser.value(QStringLiteral("auto-join-url")).toStdString();
    options.room_id =
        parser.value(QStringLiteral("auto-join-room")).toStdString();
    options.user_id =
        parser.value(QStringLiteral("auto-join-uid")).toStdString();
    options.fetch_dev_token =
        parser.isSet(QStringLiteral("auto-join-dev-token"));
    options.audio_backend = rtcwin::domain::AudioBackend::None;
    options.media_mode = rtcwin::domain::MediaModePreference::Sfu;
    window.BeginJoin(options);
    if (parser.isSet(QStringLiteral("auto-leave-ms"))) {
      bool leave_ok = false;
      const int leave_ms =
          parser.value(QStringLiteral("auto-leave-ms")).toInt(&leave_ok);
      if (leave_ok && leave_ms >= 0) {
        QTimer::singleShot(leave_ms, &window,
                           &rtcwin::ui::MainWindow::LeaveRoom);
      }
    }
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
      QPixmap pixmap = window.grab();
      if (pixmap.isNull() && window.screen() != nullptr) {
        pixmap = window.screen()->grabWindow(window.winId());
      }
      const bool saved = pixmap.save(path);
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
