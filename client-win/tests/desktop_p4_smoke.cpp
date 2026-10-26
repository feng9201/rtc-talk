#include "services/session_controller.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QTimer>

#include <iostream>

#include "log_setup.h"

namespace {

int failures = 0;

void Fail(const QString& message) {
  std::cerr << "FAIL: " << message.toStdString() << '\n';
  ++failures;
}

talk::desktop::PrejoinOptions Options(const QString& room, const QString& uid) {
  talk::desktop::PrejoinOptions options;
  options.url = QStringLiteral("ws://47.117.159.193:8080/talk/v1/ws");
  options.roomId = room;
  options.userId = uid;
  options.fetchDevToken = true;
  options.audioBackend = talk::desktop::AudioBackend::Fake;
  options.mediaMode = talk::desktop::MediaModePreference::Sfu;
  options.talkMode = talk::desktop::TalkMode::Ptt;
  return options;
}

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  talk::LogOptions log_options;
  talk::LoggingGuard logging(log_options);

  const QString suffix =
      QString::number(QDateTime::currentMSecsSinceEpoch() % 1000000);
  const QString room = QStringLiteral("desktop-p4-smoke-") + suffix;
  talk::desktop::SessionController host;
  talk::desktop::SessionController member;

  bool host_joined = false;
  bool member_joined = false;
  bool policy_updated = false;
  bool floor_granted = false;
  bool floor_released = false;
  bool mute_all_sent = false;
  bool leave_started = false;
  bool finishing = false;
  bool camera_on_seen = false;
  bool camera_off_seen = false;
  bool share_on_seen = false;
  bool share_off_seen = false;
  int video_phase = 0;

  const auto finish = [&app, &finishing](int code) {
    if (finishing) return;
    finishing = true;
    QTimer::singleShot(50, &app, [&app, code] { app.exit(code); });
  };

  QObject::connect(&host, &talk::desktop::SessionController::Joined,
                   [&] {
                     host_joined = true;
                     std::cout << "host joined\n";
                     QTimer::singleShot(
                         300, [&] { member.ConnectAndJoin(
                                        Options(room, QStringLiteral("p4-member-") +
                                                          suffix)); });
                   });
  QObject::connect(&member, &talk::desktop::SessionController::Joined,
                   [&] {
                     member_joined = true;
                     std::cout << "member joined\n";
                     video_phase = 1;
                     host.ToggleCamera();
                     QTimer::singleShot(900, [&] {
                       video_phase = 2;
                       host.ToggleCamera();
                     });
                     QTimer::singleShot(1800, [&] {
                       video_phase = 3;
                       host.ToggleShare();
                     });
                     QTimer::singleShot(2700, [&] {
                       video_phase = 4;
                       host.ToggleShare();
                     });
                     QTimer::singleShot(3600, [&] {
                       talk::desktop::RoomPolicy policy =
                           host.snapshot().policy;
                       policy.pttQueue = false;
                       host.UpdatePolicy(policy);
                       QTimer::singleShot(500, [&] { member.RequestFloor(); });
                     });
                   });

  QObject::connect(
      &host, &talk::desktop::SessionController::StateChanged,
      [&](const talk::desktop::SessionSnapshot& state) {
        if (state.policy.pttQueue == false) policy_updated = true;
        if (mute_all_sent && !leave_started) {
          bool all_muted = true;
          for (const auto& peer : state.peers) {
            if (!peer.audioMuted) all_muted = false;
          }
          if (all_muted) {
            leave_started = true;
            std::cout << "mute all confirmed\n";
            member.LeaveRoom();
          }
        }
      });
  QObject::connect(
      &member, &talk::desktop::SessionController::StateChanged,
      [&](const talk::desktop::SessionSnapshot& state) {
        const QString host_uid =
            QStringLiteral("p4-host-") + suffix;
        for (const auto& peer : state.peers) {
          if (peer.uid != host_uid) continue;
          if (video_phase == 1 && peer.cameraOn) camera_on_seen = true;
          if (video_phase == 2 && !peer.cameraOn) camera_off_seen = true;
          if (video_phase == 3 && peer.screenOn) share_on_seen = true;
          if (video_phase == 4 && !peer.screenOn) share_off_seen = true;
        }
        if (state.floor.selfHasFloor && !floor_granted) {
          floor_granted = true;
          std::cout << "floor granted\n";
          QTimer::singleShot(300, [&] { member.ReleaseFloor(); });
        } else if (floor_granted && !state.floor.selfHasFloor &&
                   !floor_released) {
          floor_released = true;
          std::cout << "floor released\n";
          QTimer::singleShot(300, [&] {
            mute_all_sent = true;
            host.MuteAll();
          });
        }
      });

  QObject::connect(
      &host, &talk::desktop::SessionController::Notice,
      [&](const QString& message, const QString&) {
        if (message.contains(QStringLiteral("房间策略"))) {
          policy_updated = true;
        }
      });
  QObject::connect(&member, &talk::desktop::SessionController::Left,
                   [&] {
                     std::cout << "member left\n";
                     host.LeaveRoom();
                   });
  QObject::connect(&host, &talk::desktop::SessionController::Left, [&] {
    std::cout << "host left\n";
    if (!host_joined) Fail(QStringLiteral("host did not join"));
    if (!member_joined) Fail(QStringLiteral("member did not join"));
    if (!policy_updated) Fail(QStringLiteral("policy update missing"));
    if (!floor_granted) Fail(QStringLiteral("floor grant missing"));
    if (!floor_released) Fail(QStringLiteral("floor release missing"));
    if (!mute_all_sent) Fail(QStringLiteral("mute all missing"));
    if (!camera_on_seen) Fail(QStringLiteral("camera on event missing"));
    if (!camera_off_seen) Fail(QStringLiteral("camera off event missing"));
    if (!share_on_seen) Fail(QStringLiteral("share on event missing"));
    if (!share_off_seen) Fail(QStringLiteral("share off event missing"));
    finish(failures == 0 ? 0 : 1);
  });

  const auto on_error = [&](const QString& message, const QString& detail) {
    std::cerr << "error: " << message.toStdString();
    if (!detail.isEmpty()) std::cerr << " " << detail.toStdString();
    std::cerr << '\n';
    finish(2);
  };
  QObject::connect(&host, &talk::desktop::SessionController::ErrorOccurred,
                   on_error);
  QObject::connect(&member, &talk::desktop::SessionController::ErrorOccurred,
                   on_error);

  QTimer::singleShot(30000, &app, [&] {
    std::cerr << "timeout host=" << host_joined << " member=" << member_joined
              << " policy=" << policy_updated << " floor=" << floor_granted
              << " released=" << floor_released
              << " mute=" << mute_all_sent
              << " cam_on=" << camera_on_seen
              << " cam_off=" << camera_off_seen
              << " share_on=" << share_on_seen
              << " share_off=" << share_off_seen << '\n';
    finish(3);
  });

  QTimer::singleShot(0, [&] {
    host.ConnectAndJoin(
        Options(room, QStringLiteral("p4-host-") + suffix));
  });
  return app.exec();
}
