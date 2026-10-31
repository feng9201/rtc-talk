#include "rtcwin/engine/reconnect_controller.h"

namespace rtcwin::engine {
namespace {

int DelayForAttempt(int attempt) {
  if (attempt <= 1) return 1;
  if (attempt == 2) return 2;
  if (attempt == 3) return 4;
  if (attempt == 4) return 8;
  return 15;
}

}  // namespace

void ReconnectController::Reset() {
  deadline_ = {};
  attempts_ = 0;
  active_ = false;
}

void ReconnectController::Begin(Clock::time_point now, int ttl_seconds) {
  deadline_ = now + std::chrono::seconds(ttl_seconds > 0 ? ttl_seconds : 60);
  attempts_ = 0;
  active_ = true;
}

ReconnectDecision ReconnectController::Next(Clock::time_point now) {
  ReconnectDecision decision;
  if (!active_) return decision;
  if (now >= deadline_) {
    active_ = false;
    decision.expired = true;
    decision.attempt = attempts_;
    return decision;
  }
  ++attempts_;
  decision.retry = true;
  decision.attempt = attempts_;
  decision.delay_seconds = DelayForAttempt(attempts_);
  return decision;
}

}  // namespace rtcwin::engine
