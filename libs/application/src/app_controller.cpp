#include "rtcwin/application/app_controller.h"

#include <QUrl>

#include <utility>

namespace rtcwin::application {

AppController::AppController(std::unique_ptr<EngineClient> engine,
                             QObject* parent)
    : QObject(parent), engine_(std::move(engine)) {
  connect(engine_.get(), &EngineClient::SnapshotChanged, this,
          &AppController::ApplySnapshot);
  connect(engine_.get(), &EngineClient::Joined, this, [this] {
    busy_ = false;
    snapshot_.connection = domain::ConnectionPhase::Ready;
    snapshot_.join = domain::JoinPhase::Joined;
    emit SnapshotChanged(snapshot_);
    emit BusyChanged(false, QStringLiteral("已进入房间"));
    emit Joined();
  });
  connect(engine_.get(), &EngineClient::Left, this, [this] {
    busy_ = false;
    snapshot_ = {};
    emit SnapshotChanged(snapshot_);
    emit BusyChanged(false, QStringLiteral("已离开房间"));
    emit Left();
  });
  connect(engine_.get(), &EngineClient::Notice, this,
          &AppController::Notice);
  connect(engine_.get(), &EngineClient::CameraDevicesChanged, this,
          &AppController::CameraDevicesChanged);
  connect(engine_.get(), &EngineClient::AudioDevicesChanged, this,
          &AppController::AudioDevicesChanged);
  connect(engine_.get(), &EngineClient::CameraPreviewChanged, this,
          &AppController::CameraPreviewChanged);
  connect(engine_.get(), &EngineClient::LocalFrameReceived, this,
          &AppController::LocalFrameReceived);
  connect(engine_.get(), &EngineClient::ErrorOccurred, this,
          [this](const domain::AppError& error) {
            busy_ = false;
            emit BusyChanged(false, QString());
            emit ErrorOccurred(QString::fromStdString(error.message),
                               QString::fromStdString(error.detail));
          });
}

AppController::~AppController() = default;

void AppController::BeginJoin(const domain::PrejoinOptions& options) {
  if (busy_) return;
  if (const auto error = Validate(options)) {
    emit ErrorOccurred(QString::fromStdString(error->message),
                       QString::fromStdString(error->detail));
    return;
  }
  settings_.Save(options);
  snapshot_.connection = domain::ConnectionPhase::Connecting;
  snapshot_.join = domain::JoinPhase::Joining;
  snapshot_.room_id = options.room_id;
  snapshot_.self_uid = options.user_id;
  snapshot_.self_display_name = options.user_id;
  snapshot_.self_role = domain::RoomRole::Host;
  snapshot_.talk_mode = options.talk_mode;
  snapshot_.policy.talk_mode = options.talk_mode;
  busy_ = true;
  emit SnapshotChanged(snapshot_);
  emit BusyChanged(true, QStringLiteral("正在连接媒体引擎…"));
  engine_->ConnectAndJoin(options);
}

void AppController::LeaveRoom() {
  if (busy_ || snapshot_.join != domain::JoinPhase::Joined) return;
  busy_ = true;
  snapshot_.join = domain::JoinPhase::Leaving;
  emit SnapshotChanged(snapshot_);
  emit BusyChanged(true, QStringLiteral("正在离开房间…"));
  engine_->LeaveRoom();
}

void AppController::ToggleMic() {
  if (!domain::CanUseRoomControls(snapshot_)) return;
  snapshot_.mic_muted = !snapshot_.mic_muted;
  engine_->SetMicEnabled(!snapshot_.mic_muted);
  emit SnapshotChanged(snapshot_);
}

void AppController::ToggleCamera() {
  if (!domain::CanUseRoomControls(snapshot_)) return;
  snapshot_.camera_on = !snapshot_.camera_on;
  emit SnapshotChanged(snapshot_);
  engine_->ToggleCamera();
}

void AppController::ToggleShare() {
  if (!domain::CanUseRoomControls(snapshot_)) return;
  snapshot_.sharing = !snapshot_.sharing;
  emit SnapshotChanged(snapshot_);
  engine_->ToggleShare();
}

void AppController::ToggleDeafened() {
  if (!domain::CanUseRoomControls(snapshot_)) return;
  snapshot_.deafened = !snapshot_.deafened;
  emit SnapshotChanged(snapshot_);
  engine_->SetDeafened(snapshot_.deafened);
}

void AppController::RequestFloor() {
  if (!domain::CanUseRoomControls(snapshot_)) return;
  engine_->RequestFloor();
}

void AppController::ReleaseFloor() {
  if (!domain::CanUseRoomControls(snapshot_)) return;
  engine_->ReleaseFloor();
}

void AppController::SelectPeer(const std::string& uid) {
  snapshot_.selected_uid = uid;
  emit SnapshotChanged(snapshot_);
  engine_->SelectPeer(uid);
}

std::optional<domain::AppError> AppController::Validate(
    const domain::PrejoinOptions& options) const {
  const QUrl url(QString::fromStdString(options.signaling_url));
  if (url.host().isEmpty() ||
      (url.scheme() != QStringLiteral("ws") &&
       url.scheme() != QStringLiteral("wss"))) {
    return domain::AppError{
        domain::ErrorDomain::Validation, 1001,
        domain::RecoveryAction::None, false, "connect",
        "信令地址无效", "请输入 ws:// 或 wss:// 地址"};
  }
  if (options.user_id.empty() || options.room_id.empty()) {
    return domain::AppError{
        domain::ErrorDomain::Validation, 1002,
        domain::RecoveryAction::None, false, "join",
        "用户标识和房间 ID 不能为空", ""};
  }
  if (!options.fetch_dev_token && options.token.empty()) {
    return domain::AppError{
        domain::ErrorDomain::Authentication, 1101,
        domain::RecoveryAction::Reauthenticate, false, "connect",
        "Token 不能为空", "请填写 Token 或启用开发凭证"};
  }
  return std::nullopt;
}

void AppController::ApplySnapshot(const domain::SessionSnapshot& snapshot) {
  snapshot_ = snapshot;
  emit SnapshotChanged(snapshot_);
}

void AppController::RefreshCameras() { engine_->RefreshCameras(); }
void AppController::RefreshAudioDevices() { engine_->RefreshAudioDevices(); }
void AppController::StartCameraPreview(int device_index) {
  engine_->StartCameraPreview(device_index);
}
void AppController::StopCameraPreview() { engine_->StopCameraPreview(); }
}  // namespace rtcwin::application
