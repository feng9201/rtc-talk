#pragma once

#include <cstdint>
#include <optional>

namespace rtc_client {

enum class SessionState {
  idle, connecting, authenticating, ready, joining, joined, leaving,
  recovering, failed, stopping, stopped
};
enum class SessionEvent {
  connect, transport_open, hello_new, hello_resumed,
  join, join_ok, join_failed, leave, leave_ok,
  network_lost, resume_expired, terminal_failure, disconnect, stop, stopped
};

struct SessionStamp {
  std::uint64_t generation{};
  std::uint64_t revision{};
  friend bool operator==(SessionStamp a, SessionStamp b) noexcept {
    return a.generation == b.generation && a.revision == b.revision;
  }
};

// Single control-executor confinement. Controllers schedule deadlines and
// invoke this pure state machine; no transport, timer or media side effects here.
// Every asynchronous transition carries BOTH generation and operation revision.
class SessionLifecycle final {
 public:
  SessionState state() const noexcept { return state_; }
  SessionStamp stamp() const noexcept { return stamp_; }
  bool has_room() const noexcept { return has_room_; }
  bool accepts(SessionStamp stamp) const noexcept { return stamp == stamp_; }

  std::optional<SessionStamp> apply(SessionEvent event) noexcept;
  std::optional<SessionStamp> complete(SessionStamp expected, SessionEvent event) noexcept;

 private:
  SessionState state_{SessionState::idle};
  SessionStamp stamp_{};
  bool has_room_{false};
  bool resuming_{false};
};

}  // namespace rtc_client
