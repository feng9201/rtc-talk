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
void UnavailableEngineClient::ToggleDeafened() {}
void UnavailableEngineClient::RequestFloor() {}
void UnavailableEngineClient::ReleaseFloor() {}
void UnavailableEngineClient::SelectPeer(const std::string& uid) {
  Q_UNUSED(uid);
}

}  // namespace rtcwin::application
