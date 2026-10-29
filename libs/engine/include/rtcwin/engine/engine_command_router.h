#pragma once

#include "rtcwin/engine/engine_command.h"

#include <functional>
#include <string>
#include <unordered_map>

namespace rtcwin::engine {

class EngineCommandRouter final {
 public:
  using Handler = std::function<void(const EngineCommand&)>;

  void Register(std::string method, Handler handler);
  bool Dispatch(const EngineCommand& command) const;

 private:
  std::unordered_map<std::string, Handler> handlers_;
};

}  // namespace rtcwin::engine
