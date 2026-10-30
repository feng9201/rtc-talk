#pragma once

#include "rtcwin/application/engine_client.h"

#include <string>
#include <vector>

namespace rtcwin::application {

class HostController final {
 public:
  explicit HostController(EngineClient* engine);

  bool Grant(const std::string& uid, bool allow);
  bool MuteAll(const std::vector<std::string>& except = {});
  bool Kick(const std::string& uid);
  void UpdatePolicy(const domain::RoomPolicyPatch& patch);

 private:
  EngineClient* engine_ = nullptr;
};

}  // namespace rtcwin::application
