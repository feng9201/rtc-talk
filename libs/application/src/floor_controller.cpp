#include "rtcwin/application/floor_controller.h"

#include <algorithm>

namespace rtcwin::application {

FloorController::FloorController(EngineClient* engine) : engine_(engine) {}

void FloorController::Sync(const domain::SessionSnapshot& snapshot) {
  snapshot_ = snapshot;
}

bool FloorController::can_request() const {
  return engine_ != nullptr &&
         domain::CanUseRoomControls(snapshot_) &&
         snapshot_.talk_mode != domain::TalkMode::Free;
}

bool FloorController::Request() {
  if (!can_request()) return false;
  engine_->RequestFloor();
  return true;
}

bool FloorController::Release() {
  if (engine_ == nullptr ||
      snapshot_.join != domain::JoinPhase::Joined) {
    return false;
  }
  engine_->ReleaseFloor();
  return true;
}

bool FloorController::Toggle() {
  if (!can_request()) return false;
  const bool queued =
      std::find(snapshot_.floor.queue.begin(),
                snapshot_.floor.queue.end(),
                snapshot_.self_uid) != snapshot_.floor.queue.end();
  if (snapshot_.floor.self_has_floor || queued) return Release();
  return Request();
}

}  // namespace rtcwin::application
