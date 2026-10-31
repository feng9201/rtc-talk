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
  void LeaveRoom();
  void ToggleMicrophone();
  void ToggleCamera();
  void ToggleScreenShare();
  void RequestFloor();
  void ReleaseFloor();
  void MuteAll();
  void GrantFloor(const QString& uid, bool allow);
  void KickMember(const QString& uid);
  void UpdateRoomPolicy(const domain::RoomPolicyPatch& patch);
  void SelectRecordingDevice(const QString& device_id);
  void SelectPlayoutDevice(const QString& device_id);
  void SelectSystemAudioDevice(const QString& device_id);
  void SelectCamera(int device_index);
  void SelectScreenSource(const QString& source_id, bool window);
  void ForceReconnect();
  void RefreshIce();
  void RestartIce(const QString& pc);
  void ExportDiagnostics();
  void SelectRoomTab(domain::PanelTab tab);
  void ShowRoomPreview();
  void ShowRoomPreviewTab(domain::PanelTab tab);
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
