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
      QStringLiteral("room-preview-tab"),
      QStringLiteral("房间预览侧栏：members|devices|status"),
      QStringLiteral("tab"),
      QStringLiteral("members")));
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
      QStringLiteral("auto-join-backend"),
      QStringLiteral("联调音频后端：wasapi|fake|none，默认 none"),
      QStringLiteral("backend")));
  parser.addOption(QCommandLineOption(
      QStringLiteral("auto-join-media-mode"),
      QStringLiteral("联调媒体模式偏好：auto|sfu|p2p，默认 sfu"),
      QStringLiteral("mode")));
  parser.addOption(QCommandLineOption(
      QStringLiteral("auto-join-audio-source"),
      QStringLiteral("联调音频采集源：mic|system，默认 mic"),
      QStringLiteral("source")));
  parser.addOption(QCommandLineOption(
      QStringLiteral("auto-join-talk-mode"),
      QStringLiteral("联调对讲模式：free|ptt|moderated，默认 free"),
      QStringLiteral("mode")));
  parser.addOption(QCommandLineOption(
      QStringLiteral("auto-join-dev-token"),
      QStringLiteral("联调用 /dev/token 获取开发凭证")));
  parser.addOption(QCommandLineOption(
      QStringLiteral("auto-unmute-ms"),
      QStringLiteral("入房后自动切换麦克风状态的时间"),
      QStringLiteral("ms")));
  parser.addOption(QCommandLineOption(
      QStringLiteral("auto-mute-ms"),
      QStringLiteral("入房后自动再次切换麦克风状态的时间"),
      QStringLiteral("ms")));
  parser.addOption(QCommandLineOption(
      QStringLiteral("auto-camera-ms"),
      QStringLiteral("入房后自动开启摄像头的时间"),
      QStringLiteral("ms")));
  parser.addOption(QCommandLineOption(
      QStringLiteral("auto-camera-stop-ms"),
      QStringLiteral("入房后自动关闭摄像头的时间"),
      QStringLiteral("ms")));
  parser.addOption(QCommandLineOption(
      QStringLiteral("auto-floor-request-ms"),
      QStringLiteral("联调：入房后自动申请发言权的时间"),
      QStringLiteral("ms")));
  parser.addOption(QCommandLineOption(
      QStringLiteral("auto-floor-release-ms"),
      QStringLiteral("联调：入房后自动释放发言权的时间"),
      QStringLiteral("ms")));
  parser.addOption(QCommandLineOption(
      QStringLiteral("auto-host-grant-uid"),
      QStringLiteral("联调：主持人自动授权的目标 UID"),
      QStringLiteral("uid")));
  parser.addOption(QCommandLineOption(
      QStringLiteral("auto-host-grant-ms"),
      QStringLiteral("联调：自动授权时间"),
      QStringLiteral("ms"),
      QStringLiteral("2500")));
  parser.addOption(QCommandLineOption(
      QStringLiteral("auto-host-grant-allow"),
      QStringLiteral("联调：授权 true 或收回 false"),
      QStringLiteral("bool"),
      QStringLiteral("true")));
  parser.addOption(QCommandLineOption(
      QStringLiteral("auto-host-mute-all-ms"),
      QStringLiteral("联调：自动全员静音时间"),
      QStringLiteral("ms")));
  parser.addOption(QCommandLineOption(
      QStringLiteral("auto-host-kick-uid"),
      QStringLiteral("联调：主持人自动移出的目标 UID"),
      QStringLiteral("uid")));
  parser.addOption(QCommandLineOption(
      QStringLiteral("auto-host-kick-ms"),
      QStringLiteral("联调：自动移出时间"),
      QStringLiteral("ms"),
      QStringLiteral("3000")));
  parser.addOption(QCommandLineOption(
      QStringLiteral("auto-host-policy-ms"),
      QStringLiteral("联调：自动修改房间策略时间"),
      QStringLiteral("ms")));
  parser.addOption(QCommandLineOption(
      QStringLiteral("auto-host-policy-talk-mode"),
      QStringLiteral("联调：修改后的对讲模式 free|ptt|moderated"),
      QStringLiteral("mode"),
      QStringLiteral("ptt")));
  parser.addOption(QCommandLineOption(
      QStringLiteral("auto-screen-ms"),
      QStringLiteral("入房后自动开启屏幕共享的时间"),
      QStringLiteral("ms")));
  parser.addOption(QCommandLineOption(
      QStringLiteral("auto-screen-stop-ms"),
      QStringLiteral("入房后自动停止屏幕共享的时间"),
      QStringLiteral("ms")));
  parser.addOption(QCommandLineOption(
      QStringLiteral("auto-select-delay-ms"),
      QStringLiteral("入房后执行设备切换的延迟"),
      QStringLiteral("ms"),
      QStringLiteral("3500")));
  parser.addOption(QCommandLineOption(
      QStringLiteral("auto-select-recording-id"),
      QStringLiteral("联调：切换到指定麦克风端点"),
      QStringLiteral("id")));
  parser.addOption(QCommandLineOption(
      QStringLiteral("auto-select-playout-id"),
      QStringLiteral("联调：切换到指定播放端点"),
      QStringLiteral("id")));
  parser.addOption(QCommandLineOption(
      QStringLiteral("auto-select-system-audio-id"),
      QStringLiteral("联调：切换到指定系统音 loopback 端点"),
      QStringLiteral("id")));
  parser.addOption(QCommandLineOption(
      QStringLiteral("auto-select-camera-index"),
      QStringLiteral("联调：切换到指定摄像头索引"),
      QStringLiteral("index")));
  parser.addOption(QCommandLineOption(
      QStringLiteral("auto-select-screen-source-id"),
      QStringLiteral("联调：切换到指定屏幕/窗口共享源"),
      QStringLiteral("id")));
  parser.addOption(QCommandLineOption(
      QStringLiteral("auto-select-screen-source-window"),
      QStringLiteral("联调：共享源类型为窗口")));
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
    const QString tab = parser.value(QStringLiteral("room-preview-tab"));
    const rtcwin::domain::PanelTab panel_tab =
        tab == QStringLiteral("devices")
            ? rtcwin::domain::PanelTab::Devices
            : tab == QStringLiteral("status")
                  ? rtcwin::domain::PanelTab::Status
                  : rtcwin::domain::PanelTab::Members;
    QTimer::singleShot(0, &window, [&window, panel_tab] {
      window.ShowRoomPreviewTab(panel_tab);
    });
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
    const QString backend = parser.value(QStringLiteral("auto-join-backend"));
    if (backend == QStringLiteral("wasapi")) {
      options.audio_backend = rtcwin::domain::AudioBackend::Wasapi;
    } else if (backend == QStringLiteral("fake")) {
      options.audio_backend = rtcwin::domain::AudioBackend::Fake;
    } else {
      options.audio_backend = rtcwin::domain::AudioBackend::None;
    }
    const QString mode = parser.value(QStringLiteral("auto-join-media-mode"));
    if (mode == QStringLiteral("auto")) {
      options.media_mode = rtcwin::domain::MediaModePreference::Auto;
    } else if (mode == QStringLiteral("p2p")) {
      options.media_mode = rtcwin::domain::MediaModePreference::P2p;
    } else {
      options.media_mode = rtcwin::domain::MediaModePreference::Sfu;
    }
    options.audio_source =
        parser.value(QStringLiteral("auto-join-audio-source")) ==
                QStringLiteral("system")
            ? rtcwin::domain::AudioSource::System
            : rtcwin::domain::AudioSource::Microphone;
    const QString talk_mode =
        parser.value(QStringLiteral("auto-join-talk-mode"));
    options.talk_mode =
        talk_mode == QStringLiteral("ptt")
            ? rtcwin::domain::TalkMode::Ptt
            : talk_mode == QStringLiteral("moderated")
                  ? rtcwin::domain::TalkMode::Moderated
                  : rtcwin::domain::TalkMode::Free;
    window.BeginJoin(options);
    if (parser.isSet(QStringLiteral("auto-unmute-ms"))) {
      bool unmute_ok = false;
      const int unmute_ms =
          parser.value(QStringLiteral("auto-unmute-ms")).toInt(&unmute_ok);
      if (unmute_ok && unmute_ms >= 0) {
        QTimer::singleShot(unmute_ms, &window,
                         &rtcwin::ui::MainWindow::ToggleMicrophone);
      }
    }
    if (parser.isSet(QStringLiteral("auto-mute-ms"))) {
      bool mute_ok = false;
      const int mute_ms =
          parser.value(QStringLiteral("auto-mute-ms")).toInt(&mute_ok);
      if (mute_ok && mute_ms >= 0) {
        QTimer::singleShot(mute_ms, &window,
                           &rtcwin::ui::MainWindow::ToggleMicrophone);
      }
    }
    if (parser.isSet(QStringLiteral("auto-camera-ms"))) {
      bool camera_ok = false;
      const int camera_ms =
          parser.value(QStringLiteral("auto-camera-ms")).toInt(&camera_ok);
      if (camera_ok && camera_ms >= 0) {
        QTimer::singleShot(camera_ms, &window,
                           &rtcwin::ui::MainWindow::ToggleCamera);
      }
    }
    if (parser.isSet(QStringLiteral("auto-camera-stop-ms"))) {
      bool camera_ok = false;
      const int camera_ms =
          parser.value(QStringLiteral("auto-camera-stop-ms"))
              .toInt(&camera_ok);
      if (camera_ok && camera_ms >= 0) {
        QTimer::singleShot(camera_ms, &window,
                           &rtcwin::ui::MainWindow::ToggleCamera);
      }
    }
    if (parser.isSet(QStringLiteral("auto-floor-request-ms"))) {
      bool floor_ok = false;
      const int floor_ms =
          parser.value(QStringLiteral("auto-floor-request-ms"))
              .toInt(&floor_ok);
      if (floor_ok && floor_ms >= 0) {
        QTimer::singleShot(floor_ms, &window,
                           &rtcwin::ui::MainWindow::RequestFloor);
      }
    }
    if (parser.isSet(QStringLiteral("auto-floor-release-ms"))) {
      bool floor_ok = false;
      const int floor_ms =
          parser.value(QStringLiteral("auto-floor-release-ms"))
              .toInt(&floor_ok);
      if (floor_ok && floor_ms >= 0) {
        QTimer::singleShot(floor_ms, &window,
                           &rtcwin::ui::MainWindow::ReleaseFloor);
      }
    }
    if (parser.isSet(QStringLiteral("auto-host-grant-uid"))) {
      bool grant_ok = false;
      const int grant_ms =
          parser.value(QStringLiteral("auto-host-grant-ms"))
              .toInt(&grant_ok);
      if (grant_ok && grant_ms >= 0) {
        const QString uid =
            parser.value(QStringLiteral("auto-host-grant-uid"));
        const bool allow =
            parser.value(QStringLiteral("auto-host-grant-allow"))
                    .trimmed()
                    .toLower() !=
                QStringLiteral("false");
        QTimer::singleShot(grant_ms, &window,
                           [&window, uid, allow] {
                             window.GrantFloor(uid, allow);
                           });
      }
    }
    if (parser.isSet(QStringLiteral("auto-host-mute-all-ms"))) {
      bool mute_ok = false;
      const int mute_ms =
          parser.value(QStringLiteral("auto-host-mute-all-ms"))
              .toInt(&mute_ok);
      if (mute_ok && mute_ms >= 0) {
        QTimer::singleShot(mute_ms, &window,
                           &rtcwin::ui::MainWindow::MuteAll);
      }
    }
    if (parser.isSet(QStringLiteral("auto-host-kick-uid"))) {
      bool kick_ok = false;
      const int kick_ms =
          parser.value(QStringLiteral("auto-host-kick-ms"))
              .toInt(&kick_ok);
      if (kick_ok && kick_ms >= 0) {
        const QString uid =
            parser.value(QStringLiteral("auto-host-kick-uid"));
        QTimer::singleShot(kick_ms, &window,
                           [&window, uid] { window.KickMember(uid); });
      }
    }
    if (parser.isSet(QStringLiteral("auto-host-policy-ms"))) {
      bool policy_ok = false;
      const int policy_ms =
          parser.value(QStringLiteral("auto-host-policy-ms"))
              .toInt(&policy_ok);
      if (policy_ok && policy_ms >= 0) {
        const QString mode =
            parser.value(QStringLiteral("auto-host-policy-talk-mode"));
        const auto talk_mode =
            mode == QStringLiteral("moderated")
                ? rtcwin::domain::TalkMode::Moderated
                : mode == QStringLiteral("free")
                      ? rtcwin::domain::TalkMode::Free
                      : rtcwin::domain::TalkMode::Ptt;
        QTimer::singleShot(policy_ms, &window, [&window, talk_mode] {
          rtcwin::domain::RoomPolicyPatch patch;
          patch.talk_mode = talk_mode;
          window.UpdateRoomPolicy(patch);
        });
      }
    }
    if (parser.isSet(QStringLiteral("auto-screen-ms"))) {
      bool screen_ok = false;
      const int screen_ms =
          parser.value(QStringLiteral("auto-screen-ms")).toInt(&screen_ok);
      if (screen_ok && screen_ms >= 0) {
        QTimer::singleShot(screen_ms, &window,
                           &rtcwin::ui::MainWindow::ToggleScreenShare);
      }
    }
    if (parser.isSet(QStringLiteral("auto-screen-stop-ms"))) {
      bool screen_ok = false;
      const int screen_ms = parser
                                .value(QStringLiteral(
                                    "auto-screen-stop-ms"))
                                .toInt(&screen_ok);
      if (screen_ok && screen_ms >= 0) {
        QTimer::singleShot(screen_ms, &window,
                           &rtcwin::ui::MainWindow::ToggleScreenShare);
      }
    }
    if (parser.isSet(QStringLiteral("auto-select-delay-ms"))) {
      bool select_ok = false;
      const int select_ms =
          parser.value(QStringLiteral("auto-select-delay-ms"))
              .toInt(&select_ok);
      if (select_ok && select_ms >= 0) {
        const QString recording =
            parser.value(QStringLiteral("auto-select-recording-id"));
        const QString playout =
            parser.value(QStringLiteral("auto-select-playout-id"));
        const QString system_audio =
            parser.value(QStringLiteral("auto-select-system-audio-id"));
        int camera = -1;
        if (parser.isSet(QStringLiteral("auto-select-camera-index"))) {
          camera =
              parser.value(QStringLiteral("auto-select-camera-index")).toInt();
        }
        const QString screen_source =
            parser.value(QStringLiteral("auto-select-screen-source-id"));
        const bool screen_window = parser.isSet(
            QStringLiteral("auto-select-screen-source-window"));
        QTimer::singleShot(select_ms, &window,
                           [&window, recording, playout, system_audio,
                            camera, screen_source, screen_window] {
                             if (!recording.isEmpty()) {
                               window.SelectRecordingDevice(recording);
                             }
                             if (!playout.isEmpty()) {
                               window.SelectPlayoutDevice(playout);
                             }
                             if (!system_audio.isEmpty()) {
                               window.SelectSystemAudioDevice(system_audio);
                             }
                             if (camera >= 0) {
                               window.SelectCamera(camera);
                             }
                             if (!screen_source.isEmpty()) {
                               window.SelectScreenSource(screen_source,
                                                         screen_window);
                             }
                           });
      }
    }
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
