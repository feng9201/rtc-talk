#include "main_window.h"
#include "theme/theme.h"

#include <QApplication>
#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QDir>
#include <QFileInfo>
#include <QScreen>
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
      {"screenshot-delay-ms", "截图前等待时间", "ms", "700"});
  parser.addOption(
      {"smoke-seconds", "指定秒数后自动退出", "seconds"});
  parser.addOption(
      {"auto-join-url", "联调：自动入房信令地址", "url"});
  parser.addOption(
      {"auto-join-room", "联调：自动入房房间 ID", "room"});
  parser.addOption(
      {"auto-join-uid", "联调：自动入房用户 ID", "uid"});
  parser.addOption(
      {"auto-join-dev-token", "联调：自动使用 /dev/token"});
  parser.addOption(
      {"auto-join-backend", "联调：音频后端 none|fake|wasapi", "name",
       "fake"});
  parser.addOption(
      {"auto-join-media-mode", "联调：媒体模式 auto|sfu|p2p", "name",
       "sfu"});
  parser.addOption(
      {"auto-join-talk-mode", "联调：对讲模式 free|ptt|moderated", "name",
       "free"});
  parser.addOption(
      {"show-policy-after-ms", "联调：入房后自动打开主持人策略弹窗", "ms"});
  parser.addOption(
      {"start-camera-preview", "启动后自动打开本地摄像头预览"});
  parser.addOption(
      {"auto-start-camera-ms", "联调：入房后自动开启摄像头", "ms"});
  parser.addOption(
      {"auto-start-share-ms", "联调：入房后自动开始屏幕共享", "ms"});
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
  if (parser.isSet("start-camera-preview")) {
    QTimer::singleShot(3000, &window,
                       &talk::desktop::MainWindow::StartLocalPreview);
  }

  if (parser.isSet("auto-join-url") && parser.isSet("auto-join-room") &&
      parser.isSet("auto-join-uid")) {
    talk::desktop::PrejoinOptions options;
    options.url = parser.value("auto-join-url");
    options.roomId = parser.value("auto-join-room");
    options.userId = parser.value("auto-join-uid");
    options.fetchDevToken = parser.isSet("auto-join-dev-token");
    const QString backend = parser.value("auto-join-backend").toLower();
    options.audioBackend = backend == QStringLiteral("none")
                               ? talk::desktop::AudioBackend::None
                               : backend == QStringLiteral("wasapi")
                                     ? talk::desktop::AudioBackend::Wasapi
                                     : talk::desktop::AudioBackend::Fake;
    const QString media_mode =
        parser.value("auto-join-media-mode").toLower();
    options.mediaMode = media_mode == QStringLiteral("p2p")
                            ? talk::desktop::MediaModePreference::P2p
                            : media_mode == QStringLiteral("auto")
                                  ? talk::desktop::MediaModePreference::Auto
                                  : talk::desktop::MediaModePreference::Sfu;
    const QString talk_mode =
        parser.value("auto-join-talk-mode").toLower();
    options.talkMode = talk_mode == QStringLiteral("ptt")
                           ? talk::desktop::TalkMode::Ptt
                           : talk_mode == QStringLiteral("moderated")
                                 ? talk::desktop::TalkMode::Moderated
                                 : talk::desktop::TalkMode::Free;
    QTimer::singleShot(0, &window, [&window, options] {
      window.BeginJoin(options);
    });
    if (parser.isSet("show-policy-after-ms")) {
      bool policy_ok = false;
      const int delay = parser.value("show-policy-after-ms").toInt(&policy_ok);
      if (policy_ok && delay >= 0) {
        QTimer::singleShot(delay, &window,
                           &talk::desktop::MainWindow::OpenRoomPolicyDialog);
      }
    }
    if (parser.isSet("auto-start-camera-ms")) {
      bool ok = false;
      const int delay = parser.value("auto-start-camera-ms").toInt(&ok);
      if (ok && delay >= 0) {
        QTimer::singleShot(delay, &window,
                           &talk::desktop::MainWindow::ToggleRoomCamera);
      }
    }
    if (parser.isSet("auto-start-share-ms")) {
      bool ok = false;
      const int delay = parser.value("auto-start-share-ms").toInt(&ok);
      if (ok && delay >= 0) {
        QTimer::singleShot(delay, &window,
                           &talk::desktop::MainWindow::ToggleRoomShare);
      }
    }
  }

  const QString screenshot = parser.value("screenshot").trimmed();
  if (!screenshot.isEmpty()) {
    bool delay_ok = false;
    const int delay_ms = parser.value("screenshot-delay-ms").toInt(&delay_ok);
    QTimer::singleShot(delay_ok && delay_ms >= 0 ? delay_ms : 700, &window,
                       [&app, &window, screenshot] {
      const QString path = QFileInfo(screenshot).absoluteFilePath();
      QDir().mkpath(QFileInfo(path).absolutePath());
      QPixmap capture;
      if (QWidget* modal = QApplication::activeModalWidget()) {
        const QRect area =
            window.frameGeometry().united(modal->frameGeometry());
        if (QScreen* current_screen = window.screen()) {
          capture = current_screen->grabWindow(
              0, area.x(), area.y(), area.width(), area.height());
        }
      }
      if (capture.isNull()) capture = window.grab();
      const bool saved = capture.save(path);
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
