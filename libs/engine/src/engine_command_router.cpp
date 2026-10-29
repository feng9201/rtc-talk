#include "rtcwin/engine/engine_command_router.h"

#include <utility>

namespace rtcwin::engine {

void EngineCommandRouter::Register(std::string method, Handler handler) {
  if (method.empty() || !handler) return;
  handlers_[std::move(method)] = std::move(handler);
}

bool EngineCommandRouter::Dispatch(const EngineCommand& command) const {
  const auto it = handlers_.find(command.method);
  if (it == handlers_.end()) return false;
  it->second(command);
  return true;
}

}  // namespace rtcwin::engine
