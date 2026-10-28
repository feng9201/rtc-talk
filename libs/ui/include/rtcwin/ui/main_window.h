#pragma once

#include "rtcwin/domain/types.h"
#include "rtcwin/ui/base/frameless_window.h"

#include <memory>

class QResizeEvent;
class QStackedWidget;

namespace rtcwin::application {
class AppController;
}

namespace rtcwin::ui {

class PrejoinPage;
class RoomPage;
class WindowControls;

class MainWindow final : public FramelessWindow {
  Q_OBJECT

 public:
  explicit MainWindow(QWidget* parent = nullptr);
  ~MainWindow() override;

  void BeginJoin(const domain::PrejoinOptions& options);
  void ShowRoomPreview();
  void EnableCameraPreview();

 protected:
  void resizeEvent(QResizeEvent* event) override;

 private:
  void BuildUi();
  void ConnectSignals();
  void ShowPrejoinPage();
  void ShowRoomPage();
  void PositionWindowControls();
  void UpdateStatus(const QString& text, const QString& tone = {});
  bool StartSelectedCameraPreview();

  QStackedWidget* stack_ = nullptr;
  PrejoinPage* prejoin_page_ = nullptr;
  RoomPage* room_page_ = nullptr;
  WindowControls* window_controls_ = nullptr;
  std::unique_ptr<application::AppController> controller_;
  bool auto_preview_ = false;
};

}  // namespace rtcwin::ui