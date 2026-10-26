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
  options.talkMode = talk::desktop::TalkMode::Free;
  return options;
}

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  talk::LogOptions log_options;
  talk::LoggingGuard logging(log_options);

  const QString suffix =
      QString::number(QDateTime::currentMSecsSinceEpoch() % 1000000);
  const QString room = QStringLiteral("desktop-p5-smoke-") + suffix;
  const QString host_uid = QStringLiteral("p5-host-") + suffix;
  const QString member_uid = QStringLiteral("p5-member-") + suffix;

  talk::desktop::SessionController host;
  talk::desktop::SessionController member;
  talk::desktop::SessionController preview;

  bool host_joined = false;
  bool member_joined = false;
  bool remote_stream_seen = false;
  bool remote_frame_seen = false;
  bool producer_clear_seen = false;
  bool share_stop_started = false;
  bool leave_started = false;
  bool finishing = false;
  bool preview_camera_found = false;
  bool preview_frame_seen = false;
  bool preview_failed = false;

  const auto finish = [&app, &finishing](int code) {
    if (finishing) return;
    finishing = true;
    QTimer::singleShot(50, &app, [&app, code] { app.exit(code); });
  };

  QObject::connect(&host, &talk::desktop::SessionController::Joined, [&] {
    host_joined = true;
    std::cout << "host joined\n";
    QTimer::singleShot(300, [&] {
      member.ConnectAndJoin(Options(room, member_uid));
    });
  });
  QObject::connect(&member, &talk::desktop::SessionController::Joined, [&] {
    member_joined = true;
    std::cout << "member joined\n";
    QTimer::singleShot(700, [&] {
      std::cout << "host starts share\n";
      host.ToggleShare();
    });
  });
  QObject::connect(
      &member, &talk::desktop::SessionController::VideoStreamsChanged,
      [&](const QVector<talk::desktop::VideoStreamView>& streams) {
        for (const auto& stream : streams) {
          if (stream.uid == host_uid &&
              stream.source == talk::desktop::VideoSource::Screen &&
              stream.receiving) {
            remote_stream_seen = true;
          }
        }
      });
  QObject::connect(
      &member, &talk::desktop::SessionController::VideoFrameReady,
      [&](const QString&, const QImage& frame) {
        if (frame.isNull() || remote_frame_seen) return;
        remote_frame_seen = true;
        std::cout << "remote video frame received\n";
        QTimer::singleShot(900, [&] {
          share_stop_started = true;
          std::cout << "host stops share\n";
          host.ToggleShare();
        });
      });
  QObject::connect(
      &member, &talk::desktop::SessionController::VideoFrameCleared,
      [&](const QString&) {
        if (!share_stop_started || leave_started) return;
        producer_clear_seen = true;
        leave_started = true;
        std::cout << "producer pause cleared frame\n";
        member.LeaveRoom();
      });
  QObject::connect(&member, &talk::desktop::SessionController::Left, [&] {
    std::cout << "member left\n";
    host.LeaveRoom();
  });
  QObject::connect(&host, &talk::desktop::SessionController::Left, [&] {
    std::cout << "host left\n";
    if (!host_joined) Fail(QStringLiteral("host did not join"));
    if (!member_joined) Fail(QStringLiteral("member did not join"));
    if (!remote_stream_seen) Fail(QStringLiteral("remote stream not registered"));
    if (!remote_frame_seen) Fail(QStringLiteral("remote frame not received"));
    if (!producer_clear_seen)
      Fail(QStringLiteral("producer pause did not clear frame"));
    if (!preview_camera_found)
      std::cout << "preview skipped: no camera enumerated\n";
    else if (!preview_frame_seen)
      std::cout << "preview skipped: camera unavailable or no frame\n";
    finish(failures == 0 ? 0 : 1);
  });

  QObject::connect(
      &preview, &talk::desktop::SessionController::CameraDevicesChanged,
      [&](const QVector<talk::desktop::CameraDevice>& devices) {
        if (devices.isEmpty()) {
          std::cout << "preview skipped: no camera device\n";
          preview_failed = true;
          return;
        }
        preview_camera_found = true;
        preview.StartCameraPreview(devices.first().index);
      });
  QObject::connect(
      &preview, &talk::desktop::SessionController::CameraPreviewChanged,
      [&](bool active, const QString&) {
        if (!active) preview_failed = true;
      });
  QObject::connect(
      &preview, &talk::desktop::SessionController::LocalVideoFrameReceived,
      [&](const QImage& frame) {
        if (!frame.isNull()) {
          preview_frame_seen = true;
          std::cout << "local preview frame received\n";
          preview.StopCameraPreview();
        }
      });
  QObject::connect(
      &preview, &talk::desktop::SessionController::ErrorOccurred,
      [&](const QString& message, const QString& detail) {
        std::cout << "preview unavailable: " << message.toStdString();
        if (!detail.isEmpty()) std::cout << " " << detail.toStdString();
        std::cout << '\n';
        preview_failed = true;
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

  QTimer::singleShot(35000, &app, [&] {
    std::cerr << "timeout host=" << host_joined
              << " member=" << member_joined
              << " stream=" << remote_stream_seen
              << " frame=" << remote_frame_seen
              << " cleared=" << producer_clear_seen
              << " preview_camera=" << preview_camera_found
              << " preview_frame=" << preview_frame_seen
              << " preview_failed=" << preview_failed << '\n';
    finish(3);
  });

  const auto preview_options = Options(QStringLiteral("preview-only"), QStringLiteral("preview"));
  preview.PrepareLocalMedia(preview_options);
  preview.EnumerateCameras();
  QTimer::singleShot(0, [&] { host.ConnectAndJoin(Options(room, host_uid)); });
  return app.exec();
}
