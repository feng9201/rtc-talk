#include "services/app_settings_service.h"

#include <QSettings>
#include <QSysInfo>

#include <memory>

namespace talk::desktop {
namespace {

std::unique_ptr<QSettings> OpenSettings(const QString& ini_path) {
  if (ini_path.isEmpty()) {
    return std::make_unique<QSettings>();
  }
  return std::make_unique<QSettings>(ini_path, QSettings::IniFormat);
}

QString AudioBackendKey(AudioBackend backend) {
  switch (backend) {
    case AudioBackend::None:
      return QStringLiteral("none");
    case AudioBackend::Fake:
      return QStringLiteral("fake");
    case AudioBackend::Wasapi:
      return QStringLiteral("wasapi");
  }
  return QStringLiteral("wasapi");
}

AudioBackend ParseAudioBackend(const QString& value) {
  if (value == QStringLiteral("none")) return AudioBackend::None;
  if (value == QStringLiteral("fake")) return AudioBackend::Fake;
  return AudioBackend::Wasapi;
}

QString MediaModeKey(MediaModePreference mode) {
  switch (mode) {
    case MediaModePreference::Sfu:
      return QStringLiteral("sfu");
    case MediaModePreference::P2p:
      return QStringLiteral("p2p");
    case MediaModePreference::Auto:
      return QStringLiteral("auto");
  }
  return QStringLiteral("auto");
}

MediaModePreference ParseMediaMode(const QString& value) {
  if (value == QStringLiteral("sfu")) return MediaModePreference::Sfu;
  if (value == QStringLiteral("p2p")) return MediaModePreference::P2p;
  return MediaModePreference::Auto;
}

QString TalkModeKey(TalkMode mode) {
  switch (mode) {
    case TalkMode::Ptt:
      return QStringLiteral("ptt");
    case TalkMode::Moderated:
      return QStringLiteral("moderated");
    case TalkMode::Free:
      return QStringLiteral("free");
  }
  return QStringLiteral("free");
}

TalkMode ParseTalkMode(const QString& value) {
  if (value == QStringLiteral("ptt")) return TalkMode::Ptt;
  if (value == QStringLiteral("moderated")) return TalkMode::Moderated;
  return TalkMode::Free;
}

QString DefaultUserId() {
  return QStringLiteral("windows-") + QSysInfo::machineHostName().toLower();
}

}  // namespace

AppSettingsService::AppSettingsService(const QString& ini_path)
    : ini_path_(ini_path) {}

PrejoinOptions AppSettingsService::Load() const {
  const auto storage = OpenSettings(ini_path_);
  PrejoinOptions options;
  options.url = storage->value(QStringLiteral("connection/url")).toString();
  options.userId =
      storage->value(QStringLiteral("connection/user_id"), DefaultUserId())
          .toString();
  options.roomId =
      storage->value(QStringLiteral("connection/room_id")).toString();
  options.fetchDevToken =
      storage->value(QStringLiteral("connection/dev_token"), false).toBool();
  options.audioBackend = ParseAudioBackend(
      storage->value(QStringLiteral("audio/backend"), QStringLiteral("wasapi"))
          .toString());
  options.mediaMode = ParseMediaMode(
      storage->value(QStringLiteral("media/mode"), QStringLiteral("auto"))
          .toString());
  options.talkMode = ParseTalkMode(
      storage->value(QStringLiteral("room/talk_mode"), QStringLiteral("free"))
          .toString());
  options.audioSource =
      storage->value(QStringLiteral("audio/source"), QStringLiteral("mic"))
                  .toString() == QStringLiteral("system")
          ? AudioSource::System
          : AudioSource::Microphone;
  options.volume =
      storage->value(QStringLiteral("audio/volume"), 60).toInt();
  options.recordingDevice =
      storage->value(QStringLiteral("audio/recording_device")).toString();
  options.playoutDevice =
      storage->value(QStringLiteral("audio/playout_device")).toString();
  options.cameraDevice =
      storage->value(QStringLiteral("video/camera_device")).toString();
  options.token.clear();
  return options;
}

void AppSettingsService::Save(const PrejoinOptions& options) const {
  const auto storage = OpenSettings(ini_path_);
  storage->setValue(QStringLiteral("connection/url"), options.url);
  storage->setValue(QStringLiteral("connection/user_id"), options.userId);
  storage->setValue(QStringLiteral("connection/room_id"), options.roomId);
  storage->setValue(QStringLiteral("connection/dev_token"),
                    options.fetchDevToken);
  storage->setValue(QStringLiteral("audio/backend"),
                    AudioBackendKey(options.audioBackend));
  storage->setValue(QStringLiteral("media/mode"),
                    MediaModeKey(options.mediaMode));
  storage->setValue(QStringLiteral("room/talk_mode"),
                    TalkModeKey(options.talkMode));
  storage->setValue(QStringLiteral("audio/source"),
                    options.audioSource == AudioSource::System
                        ? QStringLiteral("system")
                        : QStringLiteral("mic"));
  storage->setValue(QStringLiteral("audio/volume"), options.volume);
  storage->setValue(QStringLiteral("audio/recording_device"),
                    options.recordingDevice);
  storage->setValue(QStringLiteral("audio/playout_device"),
                    options.playoutDevice);
  storage->setValue(QStringLiteral("video/camera_device"),
                    options.cameraDevice);
  storage->remove(QStringLiteral("connection/token"));
  storage->sync();
}

}  // namespace talk::desktop
