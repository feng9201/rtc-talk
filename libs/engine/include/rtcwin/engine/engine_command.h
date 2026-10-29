#pragma once

#include "rtcwin/signaling/json.h"

#include <cstdint>
#include <string>

namespace rtcwin::engine {

struct EngineCommand {
  std::uint64_t id = 0;
  std::string method;
  signaling::Json data = signaling::Json::object();
};

}  // namespace rtcwin::engine
