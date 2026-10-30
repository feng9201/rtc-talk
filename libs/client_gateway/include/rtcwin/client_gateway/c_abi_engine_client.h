#pragma once

#include "rtcwin/application/engine_client.h"

#include <QJsonObject>

#include <atomic>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>

class QNetworkAccessManager;
class QTimer;

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
  void SetDeafened(bool deafened) override;
  void RequestFloor() override;
  void ReleaseFloor() override;
  void SelectPeer(const std::string& uid) override;
  void RefreshCameras() override;
  void RefreshAudioDevices() override;
  void StartCameraPreview(int device_index) override;
  void StopCameraPreview() override;

 private:
  struct CallbackContext;
  static void OnEvent(void* userdata, const char* method, const char* json);
  static void OnRemoteFrame(void* userdata, const rtcwin_frame_view* frame);
  static void OnLocalFrame(void* userdata, const rtcwin_frame_view* frame);
  uint64_t Submit(const QString& method,
                  const QJsonObject& payload = QJsonObject());
  void HandleEvent(const QString& method, const QJsonObject& event);
  void SubmitConnectAndJoin(const domain::PrejoinOptions& options,
                            const QString& token);
  void ApplyJoinData(const QJsonObject& data);
  void ApplyRoomNotification(const QString& method, const QJsonObject& data);
  void DrainLatestLocalFrames();
  void DrainLatestRemoteFrames();

  std::unique_ptr<CallbackContext> context_;
  rtcwin_handle* handle_ = nullptr;
  QNetworkAccessManager* network_ = nullptr;
  QTimer* trace_stats_timer_ = nullptr;
  domain::SessionSnapshot snapshot_;
  std::mutex local_frame_mutex_;
  std::map<std::string, domain::VideoFrame> latest_local_frames_;
  std::atomic<bool> local_frame_post_pending_{false};
  std::mutex remote_frame_mutex_;
  std::map<std::string, domain::VideoFrame> latest_remote_frames_;
  std::atomic<bool> remote_frame_post_pending_{false};
};

}  // namespace rtcwin::client_gateway
