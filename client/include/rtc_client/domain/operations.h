#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace rtc_client {

enum class OperationCode { ok, rejected, timeout, cancelled, transport_failed };
struct OperationResult {
  std::uint64_t id{}, generation{};
  std::string method;
  OperationCode code{OperationCode::cancelled};
};

// Single control-executor confinement. Results are values, not callbacks:
// callers publish them AFTER registry mutation, preventing callback reentrancy
// from invalidating iterators. A completed/cancelled ID cannot complete twice.
class Operations final {
 public:
  using Clock = std::chrono::steady_clock;
  using Time = Clock::time_point;
  explicit Operations(std::size_t capacity, std::uint64_t generation = 1)
      : capacity_(capacity), generation_(generation) {}

  std::optional<std::uint64_t> begin(std::string method, Time now, Time deadline);
  std::optional<OperationResult> finish(std::uint64_t id, std::uint64_t generation,
                                       Time now, OperationCode result);
  std::vector<OperationResult> expire(Time now);
  // No results are discarded. Caller must deliver these terminal results.
  std::vector<OperationResult> advance(std::uint64_t generation, OperationCode reason);
  std::vector<OperationResult> shutdown();
  std::size_t size() const noexcept { return pending_.size(); }
  std::uint64_t generation() const noexcept { return generation_; }

 private:
  struct Pending {
    std::string method;
    Time deadline;
  };
  std::vector<OperationResult> drain(OperationCode);
  const std::size_t capacity_;
  std::uint64_t generation_;
  std::uint64_t next_id_{1};
  bool stopped_{false};
  std::map<std::uint64_t, Pending> pending_;
};

// Backoff input time and entropy are injected, so tests never sleep. Deadline
// is set once per recovery window; failed retries must NOT extend the TTL.
class RecoveryWindow final {
 public:
  using Time = Operations::Time;
  explicit RecoveryWindow(Time deadline) : deadline_(deadline) {}
  std::optional<std::chrono::milliseconds> next_delay(Time now, std::uint32_t entropy) noexcept;
  void cancel() noexcept { cancelled_ = true; }

 private:
  const Time deadline_;
  unsigned attempt_{0};
  bool cancelled_{false};
};

}  // namespace rtc_client
