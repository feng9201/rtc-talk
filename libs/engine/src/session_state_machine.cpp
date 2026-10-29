#include "rtcwin/engine/session_state_machine.h"

#include <algorithm>

namespace rtcwin::engine {

std::uint64_t SessionStateMachine::BeginConnect(std::uint64_t command_id) {
  std::lock_guard<std::mutex> lock(mutex_);
  ++state_.generation;
  state_.phase = SessionPhase::Connecting;
  state_.media_mode = SessionMediaMode::Unknown;
  state_.media_enabled = false;
  pending_join_command_ = command_id;
  return state_.generation;
}

std::uint64_t SessionStateMachine::TakeJoinCommand(std::uint64_t generation) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (generation == 0 || generation != state_.generation) return 0;
  const std::uint64_t command_id = pending_join_command_;
  pending_join_command_ = 0;
  return command_id;
}

bool SessionStateMachine::IsCurrent(std::uint64_t generation) const {
  std::lock_guard<std::mutex> lock(mutex_);
  return generation != 0 && generation == state_.generation &&
         state_.phase != SessionPhase::Disconnected &&
         state_.phase != SessionPhase::Failed;
}

bool SessionStateMachine::IsGeneration(std::uint64_t generation) const {
  std::lock_guard<std::mutex> lock(mutex_);
  return generation != 0 && generation == state_.generation;
}

bool SessionStateMachine::MarkSignalingReady(std::uint64_t generation) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (generation != state_.generation ||
      state_.phase != SessionPhase::Connecting) {
    return false;
  }
  state_.phase = SessionPhase::SignalingReady;
  return true;
}

bool SessionStateMachine::MarkJoiningRoom(std::uint64_t generation) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (generation != state_.generation ||
      state_.phase != SessionPhase::SignalingReady) {
    return false;
  }
  state_.phase = SessionPhase::JoiningRoom;
  return true;
}

bool SessionStateMachine::MarkJoined(std::uint64_t generation,
                                     SessionMediaMode mode,
                                     bool media_enabled) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (generation != state_.generation ||
      (state_.phase != SessionPhase::JoiningRoom &&
       state_.phase != SessionPhase::SignalingReady)) {
    return false;
  }
  state_.phase = SessionPhase::Joined;
  state_.media_mode = mode;
  state_.media_enabled = media_enabled;
  return true;
}

bool SessionStateMachine::SetMediaEnabled(std::uint64_t generation,
                                          bool enabled) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (generation != state_.generation ||
      state_.phase != SessionPhase::Joined) {
    return false;
  }
  state_.media_enabled = enabled;
  return true;
}

bool SessionStateMachine::SetMediaMode(std::uint64_t generation,
                                       SessionMediaMode mode) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (generation != state_.generation ||
      state_.phase == SessionPhase::Disconnected ||
      state_.phase == SessionPhase::Failed) {
    return false;
  }
  state_.media_mode = mode;
  return true;
}

bool SessionStateMachine::MarkLeaving(std::uint64_t generation) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (generation != state_.generation ||
      state_.phase != SessionPhase::Joined) {
    return false;
  }
  state_.phase = SessionPhase::Leaving;
  state_.media_enabled = false;
  return true;
}

bool SessionStateMachine::MarkFailed(std::uint64_t generation) {
  std::lock_guard<std::mutex> lock(mutex_);
  if (generation != state_.generation) return false;
  state_.phase = SessionPhase::Failed;
  state_.media_enabled = false;
  return true;
}

void SessionStateMachine::MarkDisconnected() {
  std::lock_guard<std::mutex> lock(mutex_);
  ++state_.generation;
  state_.phase = SessionPhase::Disconnected;
  state_.media_mode = SessionMediaMode::Unknown;
  state_.media_enabled = false;
  pending_join_command_ = 0;
}

EngineSessionSnapshot SessionStateMachine::snapshot() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return state_;
}

bool SessionStateMachine::signaling_ready() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return state_.phase == SessionPhase::SignalingReady ||
         state_.phase == SessionPhase::JoiningRoom ||
         state_.phase == SessionPhase::Joined;
}

bool SessionStateMachine::joined() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return state_.phase == SessionPhase::Joined;
}

bool SessionStateMachine::media_ready() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return state_.phase == SessionPhase::Joined && state_.media_enabled;
}

SessionMediaMode SessionStateMachine::media_mode() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return state_.media_mode;
}

SessionMediaMode SessionStateMachine::ParseMediaMode(
    const std::string& value) {
  return value == "p2p" ? SessionMediaMode::P2p
                        : value == "sfu" ? SessionMediaMode::Sfu
                                          : SessionMediaMode::Unknown;
}

std::string SessionStateMachine::MediaModeName(SessionMediaMode mode) {
  switch (mode) {
    case SessionMediaMode::Sfu:
      return "sfu";
    case SessionMediaMode::P2p:
      return "p2p";
    case SessionMediaMode::Unknown:
      return "unknown";
  }
  return "unknown";
}

}  // namespace rtcwin::engine
