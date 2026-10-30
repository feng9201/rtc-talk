#include "rtcwin/application/host_controller.h"

namespace rtcwin::application {

HostController::HostController(EngineClient* engine) : engine_(engine) {}

bool HostController::Grant(const std::string& uid, bool allow) {
  if (engine_ == nullptr || uid.empty()) return false;
  engine_->GrantFloor(uid, allow);
  return true;
}

bool HostController::MuteAll(const std::vector<std::string>& except) {
  if (engine_ == nullptr) return false;
  engine_->MuteAll(except);
  return true;
}

bool HostController::Kick(const std::string& uid) {
  if (engine_ == nullptr || uid.empty()) return false;
  engine_->KickMember(uid);
  return true;
}

void HostController::UpdatePolicy(const domain::RoomPolicyPatch& patch) {
  if (engine_ != nullptr) engine_->UpdateRoomPolicy(patch);
}

}  // namespace rtcwin::application
