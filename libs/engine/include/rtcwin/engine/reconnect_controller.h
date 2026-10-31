#pragma once

#include <chrono>

namespace rtcwin::engine {

struct ReconnectDecision {
  bool retry = false;
  bool expired = false;
  int attempt = 0;
  int delay_seconds = 0;
};

// Pure backoff policy for signaling reconnects. The owner keeps the actual
// asio timer; this class only decides when and how long the next retry waits.
class ReconnectController final {
 public:
  using Clock = std::chrono::steady_clock;

  void Reset();
  void Begin(Clock::time_point now, int ttl_seconds = 60);
  ReconnectDecision Next(Clock::time_point now);

  bool active() const { return active_; }
  int attempts() const { return attempts_; }

 private:
  Clock::time_point deadline_{};
  int attempts_ = 0;
  bool active_ = false;
};

}  // namespace rtcwin::engine
