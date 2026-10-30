#pragma once

#include "rtcwin/application/engine_client.h"

namespace rtcwin::application {

// Owns the local meaning of floor actions. The server floor.state event remains
// authoritative; this controller only decides whether a user action is legal
// for the current room snapshot.
class FloorController final {
 public:
  explicit FloorController(EngineClient* engine);

  void Sync(const domain::SessionSnapshot& snapshot);
  bool Request();
  bool Release();
  bool Toggle();
  bool can_request() const;

 private:
  EngineClient* engine_ = nullptr;
  domain::SessionSnapshot snapshot_;
};

}  // namespace rtcwin::application
