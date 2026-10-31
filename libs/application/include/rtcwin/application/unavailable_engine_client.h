#pragma once

#include "rtcwin/application/engine_client.h"

namespace rtcwin::application {

// Temporary adapter used until the production engine bridge is connected.
// It keeps the UI/application boundary real instead of embedding fake logic
// inside pages.
class UnavailableEngineClient final : public EngineClient {
  Q_OBJECT

 public:
  explicit UnavailableEngineClient(QObject* parent = nullptr);

  void ConnectAndJoin(const domain::PrejoinOptions& options) override;
  void LeaveRoom() override;
  void Disconnect() override;
  void SetMicEnabled(bool enabled) override;
  void ToggleCamera() override;
  void ToggleShare() override;
  void SetDeafened(bool deafened) override;
  void RequestFloor() override;
  void ReleaseFloor() override;
  void GrantFloor(const std::string& uid, bool allow) override;
  void MuteAll(const std::vector<std::string>& except) override;
  void KickMember(const std::string& uid) override;
  void UpdateRoomPolicy(const domain::RoomPolicyPatch& patch) override;
  void SubscribeLevels(bool enabled) override;
  void SelectPeer(const std::string& uid) override;
  void RefreshCameras() override;
  void RefreshAudioDevices() override;
  void RefreshDesktopSources() override;
  void SelectCamera(int device_index) override;
  void SelectRecordingDevice(const std::string& device_id) override;
  void SelectPlayoutDevice(const std::string& device_id) override;
  void SelectSystemAudioDevice(const std::string& device_id) override;
  void SelectScreenSource(const std::string& source_id, bool window) override;
  void ForceReconnect() override;
  void RefreshIce() override;
  void RestartIce(const std::string& pc) override;
  void StartCameraPreview(int device_index) override;
  void StopCameraPreview() override;
};

}  // namespace rtcwin::application
