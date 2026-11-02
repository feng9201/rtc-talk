#include "rtcwin/application/settings_store.h"

#include "rtcwin/application/token_credential_store.h"

#include <QSettings>

namespace rtcwin::application {
namespace {

QString AudioBackendValue(domain::AudioBackend value) {
  switch (value) {
    case domain::AudioBackend::Wasapi:
      return QStringLiteral("wasapi");
    case domain::AudioBackend::Fake:
      return QStringLiteral("fake");
    case domain::AudioBackend::None:
      return QStringLiteral("none");
  }
  return QStringLiteral("wasapi");
}

domain::AudioBackend ParseAudioBackend(const QString& value) {
  if (value == QStringLiteral("fake")) return domain::AudioBackend::Fake;
  if (value == QStringLiteral("none")) return domain::AudioBackend::None;
  return domain::AudioBackend::Wasapi;
}

QString MediaModeValue(domain::MediaModePreference value) {
  if (value == domain::MediaModePreference::Sfu) return QStringLiteral("sfu");
  if (value == domain::MediaModePreference::P2p) return QStringLiteral("p2p");
  return QStringLiteral("auto");
}

domain::MediaModePreference ParseMediaMode(const QString& value) {
  if (value == QStringLiteral("sfu")) return domain::MediaModePreference::Sfu;
  if (value == QStringLiteral("p2p")) return domain::MediaModePreference::P2p;
  return domain::MediaModePreference::Auto;
}

QString TalkModeValue(domain::TalkMode value) {
  if (value == domain::TalkMode::Ptt) return QStringLiteral("ptt");
  if (value == domain::TalkMode::Moderated) {
    return QStringLiteral("moderated");
  }
  return QStringLiteral("free");
}

domain::TalkMode ParseTalkMode(const QString& value) {
  if (value == QStringLiteral("ptt")) return domain::TalkMode::Ptt;
  if (value == QStringLiteral("moderated")) return domain::TalkMode::Moderated;
  return domain::TalkMode::Free;
}

}  // namespace

class SettingsStore::Impl {
 public:
  explicit Impl(QString path) : path(std::move(path)) {}
  QString path;
};

SettingsStore::SettingsStore(QString file_path)
    : impl_(new Impl(std::move(file_path))) {}

SettingsStore::~SettingsStore() { delete impl_; }

domain::PrejoinOptions SettingsStore::Load(QString* error) const {
  QSettings settings(impl_->path, QSettings::IniFormat);
  domain::PrejoinOptions result;
  result.signaling_url =
      settings.value(QStringLiteral("connection/url")).toString().toStdString();
  result.user_id =
      settings.value(QStringLiteral("connection/user_id")).toString().toStdString();
  result.room_id =
      settings.value(QStringLiteral("connection/room_id")).toString().toStdString();
  result.fetch_dev_token =
      settings.value(QStringLiteral("connection/dev_token"), false).toBool();
  result.audio_backend = ParseAudioBackend(
      settings.value(QStringLiteral("audio/backend"), QStringLiteral("wasapi"))
          .toString());
  result.audio_source =
      settings.value(QStringLiteral("audio/source"), QStringLiteral("mic"))
                  .toString() == QStringLiteral("system")
          ? domain::AudioSource::System
          : domain::AudioSource::Microphone;
  result.media_mode = ParseMediaMode(
      settings.value(QStringLiteral("media/mode"), QStringLiteral("auto"))
          .toString());
  result.talk_mode = ParseTalkMode(
      settings.value(QStringLiteral("room/talk_mode"), QStringLiteral("free"))
          .toString());
  result.volume = settings.value(QStringLiteral("audio/volume"), 60).toInt();
  result.recording_device =
      settings.value(QStringLiteral("audio/recording_device"))
          .toString()
          .toStdString();
  result.playout_device =
      settings.value(QStringLiteral("audio/playout_device"))
          .toString()
          .toStdString();
  result.system_audio_device =
      settings.value(QStringLiteral("audio/system_audio_device"))
          .toString()
          .toStdString();
  result.camera_device =
      settings.value(QStringLiteral("video/camera_device"))
          .toString()
          .toStdString();
  result.token.clear();
  const QString target = TokenCredentialStore::TargetName(
      result.signaling_url, result.user_id, result.room_id);
  const auto stored = TokenCredentialStore::Load(target, error);
  if (stored) {
    result.token = stored->token;
  }
  return result;
}

bool SettingsStore::Save(const domain::PrejoinOptions& options,
                         QString* error) const {
  QSettings settings(impl_->path, QSettings::IniFormat);
  settings.setValue(QStringLiteral("connection/url"),
                    QString::fromStdString(options.signaling_url));
  settings.setValue(QStringLiteral("connection/user_id"),
                    QString::fromStdString(options.user_id));
  settings.setValue(QStringLiteral("connection/room_id"),
                    QString::fromStdString(options.room_id));
  settings.setValue(QStringLiteral("connection/dev_token"),
                    options.fetch_dev_token);
  settings.setValue(QStringLiteral("audio/backend"),
                    AudioBackendValue(options.audio_backend));
  settings.setValue(
      QStringLiteral("audio/source"),
      options.audio_source == domain::AudioSource::System
          ? QStringLiteral("system")
          : QStringLiteral("mic"));
  settings.setValue(QStringLiteral("media/mode"),
                    MediaModeValue(options.media_mode));
  settings.setValue(QStringLiteral("room/talk_mode"),
                    TalkModeValue(options.talk_mode));
  settings.setValue(QStringLiteral("audio/volume"), options.volume);
  settings.setValue(QStringLiteral("audio/recording_device"),
                    QString::fromStdString(options.recording_device));
  settings.setValue(QStringLiteral("audio/playout_device"),
                    QString::fromStdString(options.playout_device));
  settings.setValue(QStringLiteral("audio/system_audio_device"),
                    QString::fromStdString(options.system_audio_device));
  settings.setValue(QStringLiteral("video/camera_device"),
                    QString::fromStdString(options.camera_device));
  settings.remove(QStringLiteral("connection/token"));
  settings.sync();

  const QString target = TokenCredentialStore::TargetName(
      options.signaling_url, options.user_id, options.room_id);
  if (options.fetch_dev_token || options.token.empty()) {
    return TokenCredentialStore::Delete(target, error);
  }
  return TokenCredentialStore::Store(
      target, options.user_id, options.token,
      TokenCredentialStore::ParseJwtExpiryMs(options.token), error);
}

}  // namespace rtcwin::application
