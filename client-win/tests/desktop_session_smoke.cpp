#include "services/session_controller.h"

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDateTime>
#include <QTimer>

#include <iostream>

#include "log_setup.h"

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  QCommandLineParser parser;
  parser.addHelpOption();
  parser.addOption({"url", "Signaling ws:// URL", "url",
                    "ws://47.117.159.193:8080/talk/v1/ws"});
  parser.addOption({"room", "Room ID", "room"});
  parser.addOption({"uid", "User ID", "uid"});
  parser.process(app);

  talk::LogOptions log_options;
  log_options.console = false;
  talk::LoggingGuard logging(log_options);

  const QString suffix =
      QString::number(QDateTime::currentMSecsSinceEpoch() % 1000000);
  talk::desktop::PrejoinOptions options;
  options.url = parser.value("url");
  options.roomId = parser.isSet("room")
                       ? parser.value("room")
                       : QStringLiteral("desktop-p3-") + suffix;
  options.userId = parser.isSet("uid")
                       ? parser.value("uid")
                       : QStringLiteral("desktop-smoke-") + suffix;
  options.fetchDevToken = true;
  options.audioBackend = talk::desktop::AudioBackend::None;
  options.mediaMode = talk::desktop::MediaModePreference::Sfu;
  options.talkMode = talk::desktop::TalkMode::Free;

  talk::desktop::SessionController controller;
  bool joined = false;
  QObject::connect(
      &controller, &talk::desktop::SessionController::BusyChanged,
      [](bool busy, const QString& message) {
        std::cout << "busy=" << busy << " " << message.toStdString() << '\n';
      });
  QObject::connect(&controller, &talk::desktop::SessionController::Joined,
                   [&controller, &joined] {
                     joined = true;
                     std::cout << "joined\n";
                     QTimer::singleShot(500, &controller,
                                        &talk::desktop::SessionController::LeaveRoom);
                   });
  QObject::connect(&controller, &talk::desktop::SessionController::Left,
                   [&app] {
                     std::cout << "left\n";
                     app.exit(0);
                   });
  QObject::connect(
      &controller, &talk::desktop::SessionController::ErrorOccurred,
      [&app](const QString& message, const QString& detail) {
        std::cerr << "error: " << message.toStdString();
        if (!detail.isEmpty()) std::cerr << " detail=" << detail.toStdString();
        std::cerr << '\n';
        app.exit(2);
      });
  QObject::connect(&controller, &talk::desktop::SessionController::Kicked,
                   [&app](const QString& reason) {
                     std::cerr << "kicked: " << reason.toStdString() << '\n';
                     app.exit(3);
                   });

  QTimer::singleShot(30000, &app, [&app, &joined] {
    std::cerr << "timeout joined=" << joined << '\n';
    app.exit(4);
  });
  QTimer::singleShot(0, &controller, [&controller, options] {
    controller.ConnectAndJoin(options);
  });
  return app.exec();
}
