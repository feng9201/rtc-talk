#pragma once

#include "rtcwin/client_api.h"

#include <atomic>
#include <cstdint>
#include <string>

namespace rtcwin::engine {

class EngineCore final {
 public:
  EngineCore(const rtcwin_options& options, std::string config_json);
  ~EngineCore();

  EngineCore(const EngineCore&) = delete;
  EngineCore& operator=(const EngineCore&) = delete;

  uint64_t Submit(std::string method, std::string json);

 private:
  void EmitResponse(uint64_t id, const std::string& method, bool ok,
                    int error_code, const std::string& message);

  rtcwin_options options_{};
  std::string config_json_;
  std::atomic<uint64_t> next_id_{1};
};

}  // namespace rtcwin::engine
