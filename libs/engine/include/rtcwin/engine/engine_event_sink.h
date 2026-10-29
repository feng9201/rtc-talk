#pragma once

#include "rtcwin/client_api.h"
#include "rtcwin/signaling/json.h"

#include <cstdint>
#include <string>

namespace rtcwin::engine {

class EngineEventSink final {
 public:
  explicit EngineEventSink(const rtcwin_options& options);

  void EmitResponse(std::uint64_t id, const std::string& method, bool ok,
                    int error_code, const std::string& message,
                    const signaling::Json& data =
                        signaling::Json::object());
  void EmitNotification(const std::string& method,
                        const signaling::Json& data);

 private:
  const rtcwin_options& options_;
};

}  // namespace rtcwin::engine
