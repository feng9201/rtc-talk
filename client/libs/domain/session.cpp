#include "rtc_client/domain/session.h"

#include <limits>

namespace rtc_client {

std::optional<SessionStamp> SessionLifecycle::complete(SessionStamp expected,
                                                      SessionEvent event) noexcept {
  if (!accepts(expected)) return std::nullopt;
  return apply(event);
}

std::optional<SessionStamp> SessionLifecycle::apply(SessionEvent event) noexcept {
  using S = SessionState;
  using E = SessionEvent;
  if (state_ == S::stopped) return std::nullopt;
  auto next = state_;
  bool bump_generation = false;
  bool room = has_room_;
  bool resuming = resuming_;
  switch (event) {
    case E::connect:
      if (state_ != S::idle && state_ != S::failed) return std::nullopt;
      next = S::connecting;
      bump_generation = true;
      room = false;
      resuming = false;
      break;
    case E::transport_open:
      if (state_ != S::connecting && state_ != S::recovering) return std::nullopt;
      next = S::authenticating;
      break;
    case E::hello_new:
    case E::hello_resumed:
      if (state_ != S::authenticating) return std::nullopt;
      if (event == E::hello_resumed && !resuming) return std::nullopt;
      room = event == E::hello_resumed && resuming && room;
      next = room ? S::joined : S::ready;
      resuming = false;
      break;
    case E::join:
      if (state_ != S::ready) return std::nullopt;
      next = S::joining;
      break;
    case E::join_ok:
    case E::join_failed:
      if (state_ != S::joining) return std::nullopt;
      room = event == E::join_ok;
      next = room ? S::joined : S::ready;
      break;
    case E::leave:
      if (state_ != S::joined) return std::nullopt;
      next = S::leaving;
      break;
    case E::leave_ok:
      if (state_ != S::leaving) return std::nullopt;
      next = S::ready;
      room = false;
      break;
    case E::network_lost:
      if (state_ == S::idle || state_ == S::failed || state_ == S::stopping ||
          state_ == S::recovering) return std::nullopt;
      // A deliberate leave cannot turn into an implicit resumed room.
      if (state_ == S::leaving) room = false;
      next = S::recovering;
      bump_generation = true;
      resuming = true;
      break;
    case E::resume_expired:
      if (state_ != S::recovering && !(state_ == S::authenticating && resuming_))
        return std::nullopt;
      [[fallthrough]];
    case E::terminal_failure:
      if (state_ == S::stopping) return std::nullopt;
      next = S::failed;
      bump_generation = true;
      room = false;
      resuming = false;
      break;
    case E::disconnect:
      if (state_ == S::stopping || state_ == S::idle) return std::nullopt;
      next = S::idle;
      bump_generation = true;
      room = false;
      resuming = false;
      break;
    case E::stop:
      if (state_ == S::stopping) return std::nullopt;
      next = S::stopping;
      bump_generation = true;
      room = false;
      resuming = false;
      break;
    case E::stopped:
      if (state_ != S::stopping) return std::nullopt;
      next = S::stopped;
      break;
    default:
      return std::nullopt;
  }
  // Never permit wrap-around to validate an ancient asynchronous callback.
  const auto max = std::numeric_limits<std::uint64_t>::max();
  if (stamp_.revision == max || (bump_generation && stamp_.generation == max))
    return std::nullopt;
  ++stamp_.revision;
  if (bump_generation) ++stamp_.generation;
  state_ = next;
  has_room_ = room;
  resuming_ = resuming;
  return stamp_;
}

}  // namespace rtc_client
