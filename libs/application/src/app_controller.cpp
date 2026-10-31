#include "rtcwin/application/app_controller.h"

#include <QTimer>
#include <QUrl>

#include <algorithm>
#include <utility>

namespace rtcwin::application {

AppController::AppController(std::unique_ptr<EngineClient> engine,
                             QObject* parent)
    : QObject(parent), engine_(std::move(engine)) {
  floor_controller_ = std::make_unique<FloorController>(engine_.get());
  host_controller_ = std::make_unique<HostController>(engine_.get());
  device_refresh_timer_ = new QTimer(this);
  device_refresh_timer_->setInterval(5000);
  connect(device_refresh_timer_, &QTimer::timeout, this, [this] {
    if (!domain::CanUseRoomControls(snapshot_)) return;
    engine_->RefreshCameras();
    engine_->RefreshAudioDevices();
    engine_->RefreshDesktopSources();
  });
  connect(engine_.get(), &EngineClient::SnapshotChanged, this,
          &AppController::ApplySnapshot);
  connect(engine_.get(), &EngineClient::Joined, this, [this] {
    busy_ = false;
    snapshot_.connection = domain::ConnectionPhase::Ready;
    snapshot_.join = domain::JoinPhase::Joined;
    device_refresh_timer_->start();
    floor_controller_->Sync(snapshot_);
    emit SnapshotChanged(snapshot_);
    emit BusyChanged(false, QStringLiteral("已进入房间"));
    emit Joined();
  });
  connect(engine_.get(), &EngineClient::Left, this, [this] {
    busy_ = false;
    snapshot_ = {};
    device_refresh_timer_->stop();
    floor_controller_->Sync(snapshot_);
    emit SnapshotChanged(snapshot_);
    emit BusyChanged(false, QStringLiteral("已离开房间"));
    emit Left();
  });
  connect(engine_.get(), &EngineClient::Notice, this,
          &AppController::Notice);
  connect(engine_.get(), &EngineClient::CameraDevicesChanged, this,
          &AppController::ApplyCameraDevices);
  connect(engine_.get(), &EngineClient::AudioDevicesChanged, this,
          &AppController::ApplyAudioDevices);
  connect(engine_.get(), &EngineClient::DesktopSourcesChanged, this,
          &AppController::ApplyDesktopSources);
  connect(engine_.get(), &EngineClient::CameraPreviewChanged, this,
          &AppController::CameraPreviewChanged);
  connect(engine_.get(), &EngineClient::LocalFrameReceived, this,
          &AppController::LocalFrameReceived);
  connect(engine_.get(), &EngineClient::RemoteFrameReceived, this,
          &AppController::RemoteFrameReceived);
  connect(engine_.get(), &EngineClient::ErrorOccurred, this,
          [this](const domain::AppError& error) {
            busy_ = false;
        if (error.operation.find("select") != std::string::npos) {
          engine_->RefreshCameras();
          engine_->RefreshAudioDevices();
          engine_->RefreshDesktopSources();
        }
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
  snapshot_.audio_source = options.audio_source;
  snapshot_.self_role = domain::RoomRole::Host;
  snapshot_.talk_mode = options.talk_mode;
  snapshot_.policy.talk_mode = options.talk_mode;
  snapshot_.devices.recording_device = options.recording_device;
  snapshot_.devices.playout_device = options.playout_device;
  snapshot_.devices.system_audio_device = options.system_audio_device;
  snapshot_.devices.camera_device = options.camera_device;
  floor_controller_->Sync(snapshot_);
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
  if (snapshot_.talk_mode != domain::TalkMode::Free) return;
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
  floor_controller_->Request();
}

void AppController::ReleaseFloor() {
  if (!domain::CanUseRoomControls(snapshot_)) return;
  floor_controller_->Release();
}

void AppController::ToggleFloor() {
  if (!domain::CanUseRoomControls(snapshot_)) return;
  floor_controller_->Toggle();
}

void AppController::GrantFloor(const std::string& uid, bool allow) {
  if (!domain::CanUseRoomControls(snapshot_) ||
      snapshot_.self_role != domain::RoomRole::Host) {
    return;
  }
  host_controller_->Grant(uid, allow);
}

void AppController::MuteAll(const std::vector<std::string>& except) {
  if (!domain::CanUseRoomControls(snapshot_) ||
      snapshot_.self_role != domain::RoomRole::Host) {
    return;
  }
  host_controller_->MuteAll(except);
}

void AppController::KickMember(const std::string& uid) {
  if (!domain::CanUseRoomControls(snapshot_) ||
      snapshot_.self_role != domain::RoomRole::Host) {
    return;
  }
  host_controller_->Kick(uid);
}

void AppController::UpdateRoomPolicy(
    const domain::RoomPolicyPatch& patch) {
  if (!domain::CanUseRoomControls(snapshot_) ||
      snapshot_.self_role != domain::RoomRole::Host) {
    return;
  }
  host_controller_->UpdatePolicy(patch);
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
  floor_controller_->Sync(snapshot_);
  emit SnapshotChanged(snapshot_);
}

void AppController::ApplyCameraDevices(
    const QVector<rtcwin::domain::CameraDevice>& devices) {
  if (domain::CanUseRoomControls(snapshot_) && !devices.empty()) {
    const std::string selected = snapshot_.devices.camera_device;
    const bool found =
        std::any_of(devices.begin(), devices.end(),
                    [&selected](const domain::CameraDevice& device) {
                      return device.id == selected;
                    });
    if (selected.empty() || !found) {
      const std::string next = devices.front().id;
      bool valid = false;
      const int index = QString::fromStdString(next).toInt(&valid);
      if (valid && index >= 0) {
        snapshot_.devices.camera_device = next;
        engine_->SelectCamera(index);
        emit SnapshotChanged(snapshot_);
      }
    }
  }
  emit CameraDevicesChanged(devices);
}

void AppController::ApplyAudioDevices(
    const QVector<rtcwin::domain::AudioDevice>& recording,
    const QVector<rtcwin::domain::AudioDevice>& playout) {
  if (domain::CanUseRoomControls(snapshot_)) {
    const auto missing = [](const QVector<domain::AudioDevice>& devices,
                            const std::string& selected) {
      return !selected.empty() &&
             std::none_of(devices.begin(), devices.end(),
                          [&selected](const domain::AudioDevice& device) {
                            return device.id == selected;
                          });
    };
    if (!recording.empty() &&
        missing(recording, snapshot_.devices.recording_device)) {
      snapshot_.devices.recording_device = recording.front().id;
      engine_->SelectRecordingDevice(recording.front().id);
      emit SnapshotChanged(snapshot_);
    }
    if (!playout.empty() &&
        missing(playout, snapshot_.devices.playout_device)) {
      snapshot_.devices.playout_device = playout.front().id;
      engine_->SelectPlayoutDevice(playout.front().id);
      emit SnapshotChanged(snapshot_);
    }
    if (!playout.empty() &&
        missing(playout, snapshot_.devices.system_audio_device)) {
      snapshot_.devices.system_audio_device = playout.front().id;
      if (snapshot_.audio_source == domain::AudioSource::System) {
        engine_->SelectSystemAudioDevice(playout.front().id);
      }
      emit SnapshotChanged(snapshot_);
    }
  }
  emit AudioDevicesChanged(recording, playout);
}

void AppController::ApplyDesktopSources(
    const QVector<rtcwin::domain::DesktopSource>& sources) {
  if (domain::CanUseRoomControls(snapshot_) && !sources.empty() &&
      !snapshot_.devices.screen_source.empty()) {
    const bool found =
        std::any_of(sources.begin(), sources.end(),
                    [this](const domain::DesktopSource& source) {
                      return source.id == snapshot_.devices.screen_source &&
                             source.window ==
                                 snapshot_.devices.screen_source_window;
                    });
    if (!found) {
      snapshot_.devices.screen_source = sources.front().id;
      snapshot_.devices.screen_source_window = sources.front().window;
      engine_->SelectScreenSource(sources.front().id, sources.front().window);
      emit SnapshotChanged(snapshot_);
    }
  }
  emit DesktopSourcesChanged(sources);
}

void AppController::RefreshCameras() { engine_->RefreshCameras(); }
void AppController::RefreshAudioDevices() { engine_->RefreshAudioDevices(); }
void AppController::RefreshDesktopSources() {
  engine_->RefreshDesktopSources();
}
void AppController::SelectCamera(int device_index) {
  if (!domain::CanUseRoomControls(snapshot_)) return;
  snapshot_.devices.camera_device = std::to_string(device_index);
  engine_->SelectCamera(device_index);
  emit SnapshotChanged(snapshot_);
}
void AppController::SelectRecordingDevice(const std::string& device_id) {
  if (!domain::CanUseRoomControls(snapshot_)) return;
  snapshot_.devices.recording_device = device_id;
  engine_->SelectRecordingDevice(device_id);
  emit SnapshotChanged(snapshot_);
}
void AppController::SelectPlayoutDevice(const std::string& device_id) {
  if (!domain::CanUseRoomControls(snapshot_)) return;
  snapshot_.devices.playout_device = device_id;
  engine_->SelectPlayoutDevice(device_id);
  emit SnapshotChanged(snapshot_);
}
void AppController::SelectSystemAudioDevice(const std::string& device_id) {
  if (!domain::CanUseRoomControls(snapshot_)) return;
  snapshot_.devices.system_audio_device = device_id;
  engine_->SelectSystemAudioDevice(device_id);
  emit SnapshotChanged(snapshot_);
}
void AppController::SelectScreenSource(const std::string& source_id,
                                       bool window) {
  if (!domain::CanUseRoomControls(snapshot_)) return;
  snapshot_.devices.screen_source = source_id;
  snapshot_.devices.screen_source_window = window;
  engine_->SelectScreenSource(source_id, window);
  emit SnapshotChanged(snapshot_);
}
void AppController::StartCameraPreview(int device_index) {
  engine_->StartCameraPreview(device_index);
}
void AppController::StopCameraPreview() { engine_->StopCameraPreview(); }
}  // namespace rtcwin::application
