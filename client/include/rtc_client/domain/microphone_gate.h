#pragma once

#include <atomic>
#include <cstdint>
#include <limits>

namespace rtc_client {

// The control plane checks authorization, device and room policy before opening.
// A ticket is invalidated by EVERY stop/revoke, even if already muted. The audio
// callback only reads is_open(); it never waits for a controller or a mutex.
class MicrophoneGate final {
 public:
  using Ticket = std::uint64_t;
  static_assert(std::atomic<Ticket>::is_always_lock_free,
                "Microphone safety gate requires lock-free 64-bit atomics");

  Ticket close() noexcept {
    auto old = state_.load(std::memory_order_acquire);
    for (;;) {
      const auto base = old & ~Ticket{1};
      // Saturation is permanently closed; never wrap into a valid old ticket.
      const auto next = base >= terminal - 2 ? terminal : base + 2;
      if (state_.compare_exchange_weak(old, next, std::memory_order_acq_rel,
                                       std::memory_order_acquire)) {
        return next;
      }
    }
  }

  Ticket begin_open_request() noexcept { return close(); }

  bool open_if_current(Ticket ticket) noexcept {
    if ((ticket & 1) || ticket == 0 || ticket == terminal) return false;
    return state_.compare_exchange_strong(ticket, ticket | 1,
                                          std::memory_order_acq_rel,
                                          std::memory_order_acquire);
  }

  bool is_open() const noexcept {
    return (state_.load(std::memory_order_acquire) & 1) != 0;
  }

 private:
  static constexpr Ticket terminal = std::numeric_limits<Ticket>::max() - 1;
  std::atomic<Ticket> state_{0};
};

}  // namespace rtc_client
