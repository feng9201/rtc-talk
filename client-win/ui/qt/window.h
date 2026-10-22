// File: ui/qt/window.h
// Brief: Qt 主窗口：只负责控件、用户操作与事件展示。
#ifndef TALK_UI_WINDOW_H_
#define TALK_UI_WINDOW_H_

#include <QJsonObject>
#include <QMap>
#include <QString>
#include <QWidget>

#include "app_settings.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QNetworkAccessManager;
class QPlainTextEdit;
class QPushButton;
class QSlider;
class CoreClient;

class TalkWindow : public QWidget {
  Q_OBJECT

 public:
  explicit TalkWindow(QWidget* parent = nullptr);
  ~TalkWindow() override;

  // 在日志区显示日志文件路径，便于用户回传排障。
  void SetLogPath(const QString& path);

  // 命令行临时覆盖音频后端：只对本次启动生效，不写回配置。
  void SetBackendOverride(const QString& backend);

 private:
  void BuildUi();
  void ApplySettings();
  void ConnectSignals();
  QJsonObject CoreConfig() const;

  bool EnsureCore();
  void SendCommand(const QString& method,
                   const QJsonObject& data = QJsonObject());
  void ConnectServer();
  void RefreshDevices();
  void UpdateSourceControls();
  void ResetButtons();
  void RefreshMembers();
  void AppendLog(const QString& text);

  void HandleEvent(const QString& name, const QJsonObject& data);
  void HandleState(const QJsonObject& data);
  void HandleJoined(const QJsonObject& data);
  void HandleResult(const QJsonObject& data);
  void HandleMediaError(const QString& name, const QJsonObject& data);

  CoreClient* core_ = nullptr;
  AppSettings settings_;
  QString backend_override_;
  QString self_uid_;
  QString active_source_ = QStringLiteral("mic");
  QMap<QString, QJsonObject> peers_;
  bool muted_ = true;

  QLineEdit* url_ = nullptr;
  QLineEdit* uid_ = nullptr;
  QLineEdit* room_ = nullptr;
  QLineEdit* token_ = nullptr;
  QCheckBox* dev_ = nullptr;
  QComboBox* backend_ = nullptr;
  QComboBox* mode_ = nullptr;
  QComboBox* source_ = nullptr;
  QComboBox* recording_ = nullptr;
  QComboBox* playout_ = nullptr;
  QPushButton* connect_ = nullptr;
  QPushButton* disconnect_ = nullptr;
  QPushButton* join_ = nullptr;
  QPushButton* leave_ = nullptr;
  QPushButton* mute_ = nullptr;
  QPushButton* devices_ = nullptr;
  QLabel* state_ = nullptr;
  QLabel* media_ = nullptr;
  QListWidget* members_ = nullptr;
  QSlider* volume_ = nullptr;
  QPlainTextEdit* log_ = nullptr;
  QNetworkAccessManager* http_ = nullptr;
};

#endif  // TALK_UI_WINDOW_H_
