#pragma once
/**
 * @brief:会话状态和 generation 隔离
*/
#include <cstdint>
#include <mutex>
#include <string>

namespace rtcwin::engine {

enum class SessionPhase {
  Disconnected,
  Connecting,
  SignalingReady,
  JoiningRoom,
  Joined,
  Reconnecting,
  Leaving,
  Failed,
};

enum class SessionMediaMode {
  Unknown,
  Sfu,
  P2p,
};

struct EngineSessionSnapshot {
  SessionPhase phase = SessionPhase::Disconnected;
  SessionMediaMode media_mode = SessionMediaMode::Unknown;
  bool media_enabled = false;
  std::uint64_t generation = 0;
};

class SessionStateMachine final {
 public:
  std::uint64_t BeginConnect(std::uint64_t command_id = 0);
  std::uint64_t TakeJoinCommand(std::uint64_t generation);
  bool IsCurrent(std::uint64_t generation) const;
  bool IsGeneration(std::uint64_t generation) const;

  bool MarkSignalingReady(std::uint64_t generation);
  bool MarkJoiningRoom(std::uint64_t generation);
  bool MarkJoined(std::uint64_t generation, SessionMediaMode mode,
                  bool media_enabled);
  bool MarkReconnecting(std::uint64_t generation);
  bool MarkResumed(std::uint64_t generation);
  bool SetMediaEnabled(std::uint64_t generation, bool enabled);
  bool SetMediaMode(std::uint64_t generation, SessionMediaMode mode);
  bool MarkLeaving(std::uint64_t generation);
  bool MarkFailed(std::uint64_t generation);
  void MarkDisconnected();

  EngineSessionSnapshot snapshot() const;
  bool signaling_ready() const;
  bool joined() const;
  bool media_ready() const;
  SessionMediaMode media_mode() const;

  static SessionMediaMode ParseMediaMode(const std::string& value);
  static std::string MediaModeName(SessionMediaMode mode);

 private:
  mutable std::mutex mutex_;
  EngineSessionSnapshot state_;
  std::uint64_t pending_join_command_ = 0;
};

}  // namespace rtcwin::engine
