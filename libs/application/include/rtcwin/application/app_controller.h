#pragma once

#include "rtcwin/application/engine_client.h"
#include "rtcwin/application/settings_store.h"

#include <QObject>
#include <QVector>

#include <memory>

namespace rtcwin::application {

class AppController final : public QObject {
  Q_OBJECT

 public:
  explicit AppController(std::unique_ptr<EngineClient> engine,
                         QObject* parent = nullptr);
  ~AppController() override;

  const domain::SessionSnapshot& snapshot() const { return snapshot_; }
  SettingsStore& settings() { return settings_; }

 public slots:
  void BeginJoin(const domain::PrejoinOptions& options);
  void LeaveRoom();
  void ToggleMic();
  void ToggleCamera();
  void ToggleShare();
  void ToggleDeafened();
  void RequestFloor();
  void ReleaseFloor();
  void SelectPeer(const std::string& uid);
  void RefreshCameras();
  void RefreshAudioDevices();
  void StartCameraPreview(int device_index);
  void StopCameraPreview();

 signals:
  void SnapshotChanged(const rtcwin::domain::SessionSnapshot& snapshot);
  void Joined();
  void Left();
  void BusyChanged(bool busy, const QString& message);
  void ErrorOccurred(const QString& message, const QString& detail);
  void Notice(const QString& message, const QString& tone);
  void CameraDevicesChanged(
      const QVector<rtcwin::domain::CameraDevice>& devices);
  void AudioDevicesChanged(
      const QVector<rtcwin::domain::AudioDevice>& recording,
      const QVector<rtcwin::domain::AudioDevice>& playout);
  void CameraPreviewChanged(bool active, const QString& device, int width,
                            int height, int fps);
  void LocalFrameReceived(const rtcwin::domain::VideoFrame& frame);

 private:
  std::optional<domain::AppError> Validate(
      const domain::PrejoinOptions& options) const;
  void ApplySnapshot(const domain::SessionSnapshot& snapshot);

  std::unique_ptr<EngineClient> engine_;
  SettingsStore settings_;
  domain::SessionSnapshot snapshot_;
  bool busy_ = false;
};

}  // namespace rtcwin::application
