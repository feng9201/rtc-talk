#pragma once

#include "rtcwin/domain/types.h"

#include <QObject>
#include <QVector>

namespace rtcwin::application {

class EngineClient : public QObject {
  Q_OBJECT

 public:
  explicit EngineClient(QObject* parent = nullptr) : QObject(parent) {}
  ~EngineClient() override = default;

  EngineClient(const EngineClient&) = delete;
  EngineClient& operator=(const EngineClient&) = delete;

  virtual void ConnectAndJoin(const domain::PrejoinOptions& options) = 0;
  virtual void LeaveRoom() = 0;
  virtual void Disconnect() = 0;
  virtual void SetMicEnabled(bool enabled) = 0;
  virtual void ToggleCamera() = 0;
  virtual void ToggleShare() = 0;
  virtual void SetDeafened(bool deafened) = 0;
  virtual void RequestFloor() = 0;
  virtual void ReleaseFloor() = 0;
  virtual void GrantFloor(const std::string& uid, bool allow) = 0;
  virtual void MuteAll(const std::vector<std::string>& except) = 0;
  virtual void KickMember(const std::string& uid) = 0;
  virtual void UpdateRoomPolicy(const domain::RoomPolicyPatch& patch) = 0;
  virtual void SubscribeLevels(bool enabled) = 0;
  virtual void SelectPeer(const std::string& uid) = 0;
  virtual void RefreshCameras() = 0;
  virtual void RefreshAudioDevices() = 0;
  virtual void RefreshDesktopSources() = 0;
  virtual void SelectCamera(int device_index) = 0;
  virtual void SelectRecordingDevice(const std::string& device_id) = 0;
  virtual void SelectPlayoutDevice(const std::string& device_id) = 0;
  virtual void SelectSystemAudioDevice(const std::string& device_id) = 0;
  virtual void SelectScreenSource(const std::string& source_id,
                                  bool window) = 0;
  virtual void ForceReconnect() = 0;
  virtual void RefreshIce() = 0;
  virtual void RestartIce(const std::string& pc) = 0;
  virtual void StartCameraPreview(int device_index) = 0;
  virtual void StopCameraPreview() = 0;

 signals:
  void SnapshotChanged(const rtcwin::domain::SessionSnapshot& snapshot);
  void Joined();
  void Left();
  void Notice(const QString& message, const QString& tone);
  void ErrorOccurred(const rtcwin::domain::AppError& error);
  void CameraDevicesChanged(
      const QVector<rtcwin::domain::CameraDevice>& devices);
  void AudioDevicesChanged(
      const QVector<rtcwin::domain::AudioDevice>& recording,
      const QVector<rtcwin::domain::AudioDevice>& playout);
  void DesktopSourcesChanged(
      const QVector<rtcwin::domain::DesktopSource>& sources);
  void CameraPreviewChanged(bool active, const QString& device, int width,
                            int height, int fps);
  void LocalFrameReceived(const rtcwin::domain::VideoFrame& frame);
  void RemoteFrameReceived(const rtcwin::domain::VideoFrame& frame);
};

}  // namespace rtcwin::application
