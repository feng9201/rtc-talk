#pragma once
#include "talk/talk.h"
#include <QWidget>
#include <QJsonObject>
#include <QMap>
#include <memory>
class QLineEdit;class QComboBox;class QPushButton;class QLabel;class QListWidget;class QPlainTextEdit;class QCheckBox;class QSlider;class QNetworkAccessManager;
class TalkWindow : public QWidget {
  Q_OBJECT
 public:
  TalkWindow();
  ~TalkWindow() override;
 private:
  static void core_event(void*,const char*,const char*);
  void receive(const QString&,const QJsonObject&);
  bool create_core();
  void connect_server();
  void command(const char*,const QJsonObject& data=QJsonObject());
  void refresh_members();
  void reset_buttons();
  void enter_room(const QString& room);
  void apply_mode(const QJsonObject& data);
  void update_call_buttons();
  struct CoreContext { TalkWindow* window; uint64_t generation; };
  std::unique_ptr<CoreContext> context_;
  uint64_t generation_=0;
  QString self_uid_;
  talk_handle* core_=nullptr;
  QLineEdit *url_,*uid_,*room_,*token_,*callee_;
  QComboBox *backend_,*recording_,*playout_,*mode_;
  QPushButton *connect_,*disconnect_,*join_,*leave_,*mute_,*devices_,*call_,*accept_,*hangup_;
  QLabel *state_,*media_,*route_;
  QListWidget* members_;
  QPlainTextEdit* log_;
  QCheckBox* dev_;
  QSlider* volume_;
  QNetworkAccessManager* http_;
  QMap<QString,QJsonObject> peers_;
  QString call_id_,call_room_,media_mode_="sfu",p2p_role_,p2p_peer_,p2p_path_;
  bool muted_=true,incoming_=false;
};
