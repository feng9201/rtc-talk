#pragma once

#include "base/frameless_window.h"
#include "domain/app_types.h"
#include "services/app_settings_service.h"

#include <memory>

class QResizeEvent;
class QStackedWidget;

namespace talk::desktop {

class PrejoinPage;
class RoomPage;
class SessionController;
class WindowControls;

class MainWindow final : public FramelessWindow {
  Q_OBJECT

 public:
  explicit MainWindow(QWidget* parent = nullptr);

  void BeginJoin(const PrejoinOptions& options);
  void OpenRoomPolicyDialog();
  void ShowRoomPreview();
  void StartLocalPreview();
  void ToggleRoomCamera();
  void ToggleRoomShare();
  void SetLogPath(const QString& path);

 signals:
  void joinRequested(const PrejoinOptions& options);

 protected:
  void resizeEvent(QResizeEvent* event) override;

 private:
  void BuildUi();
  void ConnectSignals();
  void PositionWindowControls();
  void ShowPrejoinPage();
  void ShowRoomPage();
  void UpdatePrejoinStatus(const QString& message, const QString& tone);

  QStackedWidget* stack_ = nullptr;
  PrejoinPage* prejoin_page_ = nullptr;
  RoomPage* room_page_ = nullptr;
  WindowControls* window_controls_ = nullptr;
  SessionController* controller_ = nullptr;
  std::unique_ptr<AppSettingsService> settings_;
  QString log_path_;
};

}  // namespace talk::desktop
