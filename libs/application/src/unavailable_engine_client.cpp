#include "rtcwin/application/unavailable_engine_client.h"

#include <QTimer>

namespace rtcwin::application {

UnavailableEngineClient::UnavailableEngineClient(QObject* parent)
    : EngineClient(parent) {}

void UnavailableEngineClient::ConnectAndJoin(
    const domain::PrejoinOptions& options) {
  Q_UNUSED(options);
  QTimer::singleShot(0, this, [this] {
    domain::AppError error;
    error.domain = domain::ErrorDomain::Internal;
    error.code = 1901;
    error.action = domain::RecoveryAction::None;
    error.operation = "connect";
    error.message = "媒体引擎尚未接入";
    error.detail = "当前版本完成了 UI 与应用层骨架。";
    emit ErrorOccurred(error);
  });
}

void UnavailableEngineClient::LeaveRoom() { emit Left(); }
void UnavailableEngineClient::Disconnect() { emit Left(); }
void UnavailableEngineClient::SetMicEnabled(bool enabled) { Q_UNUSED(enabled); }
void UnavailableEngineClient::ToggleCamera() {}
void UnavailableEngineClient::ToggleShare() {}
void UnavailableEngineClient::SetDeafened(bool deafened) { Q_UNUSED(deafened); }
void UnavailableEngineClient::RequestFloor() {}
void UnavailableEngineClient::ReleaseFloor() {}
void UnavailableEngineClient::GrantFloor(const std::string& uid, bool allow) {
  Q_UNUSED(uid);
  Q_UNUSED(allow);
}
void UnavailableEngineClient::MuteAll(
    const std::vector<std::string>& except) {
  Q_UNUSED(except);
}
void UnavailableEngineClient::KickMember(const std::string& uid) {
  Q_UNUSED(uid);
}
void UnavailableEngineClient::UpdateRoomPolicy(
    const domain::RoomPolicyPatch& patch) {
  Q_UNUSED(patch);
}
void UnavailableEngineClient::SubscribeLevels(bool enabled) {
  Q_UNUSED(enabled);
}
void UnavailableEngineClient::RefreshCameras() {}
void UnavailableEngineClient::RefreshAudioDevices() {}
void UnavailableEngineClient::RefreshDesktopSources() {}
void UnavailableEngineClient::SelectCamera(int device_index) {
  Q_UNUSED(device_index);
}
void UnavailableEngineClient::SelectRecordingDevice(
    const std::string& device_id) {
  Q_UNUSED(device_id);
}
void UnavailableEngineClient::SelectPlayoutDevice(
    const std::string& device_id) {
  Q_UNUSED(device_id);
}
void UnavailableEngineClient::SelectSystemAudioDevice(
    const std::string& device_id) {
  Q_UNUSED(device_id);
}
void UnavailableEngineClient::SelectScreenSource(
    const std::string& source_id, bool window) {
  Q_UNUSED(source_id);
  Q_UNUSED(window);
}
void UnavailableEngineClient::StartCameraPreview(int device_index) { Q_UNUSED(device_index); }
void UnavailableEngineClient::StopCameraPreview() {}

void UnavailableEngineClient::SelectPeer(const std::string& uid) {
  Q_UNUSED(uid);
}

}  // namespace rtcwin::application
