// File: ui/qt/app_settings.cpp
// Brief: 使用 QSettings 持久化界面偏好。
#include "app_settings.h"

#include <QHostInfo>
#include <QSettings>

namespace {

QString DefaultUid() {
  return QStringLiteral("windows-") + QHostInfo::localHostName().toLower();
}

}  // namespace

AppSettings AppSettings::Load() {
  QSettings storage;
  AppSettings settings;
  settings.url = storage.value(
      "url", QStringLiteral("ws://47.117.159.193:8080/talk/v1/ws"))
                     .toString();
  settings.uid = storage.value("uid", DefaultUid()).toString();
  settings.room =
      storage.value("room", QStringLiteral("rk-client-test")).toString();
  settings.backend =
      storage.value("backend", QStringLiteral("wasapi")).toString();
  settings.media_mode =
      storage.value("media_mode", QStringLiteral("auto")).toString();
  settings.talk_mode =
      storage.value("talk_mode", QStringLiteral("free")).toString();
  settings.audio_source =
      storage.value("audio_source", QStringLiteral("mic")).toString();
  settings.recording_device =
      storage.value("recording_device").toString();
  settings.playout_device = storage.value("playout_device").toString();
  return settings;
}

void AppSettings::Save() const {
  QSettings storage;
  storage.setValue("url", url);
  storage.setValue("uid", uid);
  storage.setValue("room", room);
  storage.setValue("backend", backend);
  storage.setValue("media_mode", media_mode);
  storage.setValue("talk_mode", talk_mode);
  storage.setValue("audio_source", audio_source);
  storage.setValue("recording_device", recording_device);
  storage.setValue("playout_device", playout_device);
}
