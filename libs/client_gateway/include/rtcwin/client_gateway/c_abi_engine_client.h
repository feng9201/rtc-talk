#pragma once

#include "rtcwin/application/engine_client.h"

#include <QJsonObject>

#include <cstdint>
#include <memory>

struct rtcwin_handle;
struct rtcwin_frame_view;

namespace rtcwin::client_gateway {

class CAbiEngineClient final : public application::EngineClient {
  Q_OBJECT

 public:
  explicit CAbiEngineClient(QObject* parent = nullptr);
  ~CAbiEngineClient() override;

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

 private:
  struct CallbackContext;
  static void OnEvent(void* userdata, const char* method, const char* json);
  static void OnRemoteFrame(void* userdata, const rtcwin_frame_view* frame);
  static void OnLocalFrame(void* userdata, const rtcwin_frame_view* frame);
  uint64_t Submit(const QString& method,
                  const QJsonObject& payload = QJsonObject());
  void HandleEvent(const QString& method, const QJsonObject& event);

  std::unique_ptr<CallbackContext> context_;
  rtcwin_handle* handle_ = nullptr;
};

}  // namespace rtcwin::client_gateway
