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
  void ToggleDeafened() override;
  void RequestFloor() override;
  void ReleaseFloor() override;
  void SelectPeer(const std::string& uid) override;
};

}  // namespace rtcwin::application
