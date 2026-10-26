#pragma once

#include "base/frameless_window.h"
#include "domain/app_types.h"

class QResizeEvent;
class QStackedWidget;

namespace talk::desktop {

class PrejoinPage;
class RoomPage;
class WindowControls;

class MainWindow final : public FramelessWindow {
  Q_OBJECT

 public:
  explicit MainWindow(QWidget* parent = nullptr);

  void ShowRoomPreview();
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

  QStackedWidget* stack_ = nullptr;
  PrejoinPage* prejoin_page_ = nullptr;
  RoomPage* room_page_ = nullptr;
  WindowControls* window_controls_ = nullptr;
  QString log_path_;
};

}  // namespace talk::desktop
