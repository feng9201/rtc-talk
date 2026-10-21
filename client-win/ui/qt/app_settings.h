// File: ui/qt/app_settings.h
// Brief: 界面偏好的读取与保存；Token 不落盘。
#ifndef TALK_UI_APP_SETTINGS_H_
#define TALK_UI_APP_SETTINGS_H_

#include <QString>

// 纯数据对象；由界面在构造时加载、退出或修改后保存。
struct AppSettings {
  QString url;
  QString uid;
  QString room;
  QString backend;
  QString media_mode;
  QString audio_source;
  QString recording_device;
  QString playout_device;

  static AppSettings Load();
  void Save() const;
};

#endif  // TALK_UI_APP_SETTINGS_H_
