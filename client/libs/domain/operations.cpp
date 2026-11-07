#include "rtc_client/domain/operations.h"

#include <algorithm>
#include <limits>
#include <utility>

namespace rtc_client {

std::optional<std::uint64_t> Operations::begin(std::string method, Time now, Time deadline) {
  if (stopped_ || pending_.size() >= capacity_ || next_id_ == 0 || generation_ == 0 ||
      method.empty() || method.size() > 128 || deadline <= now) return std::nullopt;
  const auto id = next_id_;
  pending_.emplace(id, Pending{std::move(method), deadline});
  next_id_ = id == std::numeric_limits<std::uint64_t>::max() ? 0 : id + 1;
  return id;
}

std::optional<OperationResult> Operations::finish(std::uint64_t id, std::uint64_t generation,
                                                 Time now, OperationCode result) {
  if (generation != generation_) return std::nullopt;
  auto it = pending_.find(id);
  if (it == pending_.end()) return std::nullopt;
  // A late response cannot win just because its callback beat the timer's
  // dispatch in the executor queue. The actual monotonic deadline is decisive.
  OperationResult done{id, generation_, it->second.method,
                       now >= it->second.deadline ? OperationCode::timeout : result};
  pending_.erase(it);
  return done;
}

std::vector<OperationResult> Operations::expire(Time now) {
  std::vector<OperationResult> results;
  results.reserve(pending_.size());
  // Construct all return values before mutation, preserving terminal outcomes
  // even if allocation/copy throws. Never erase operations incrementally first.
  for (const auto& entry : pending_) {
    if (entry.second.deadline <= now)
      results.push_back({entry.first, generation_, entry.second.method, OperationCode::timeout});
  }
  for (const auto& result : results) pending_.erase(result.id);
  return results;
}

std::vector<OperationResult> Operations::drain(OperationCode reason) {
  std::vector<OperationResult> results;
  results.reserve(pending_.size());
  for (const auto& entry : pending_)
    results.push_back({entry.first, generation_, entry.second.method, reason});
  pending_.clear();
  return results;
}

std::vector<OperationResult> Operations::advance(std::uint64_t generation, OperationCode reason) {
  if (stopped_ || generation <= generation_) return {};
  auto results = drain(reason);
  generation_ = generation;
  return results;
}

std::vector<OperationResult> Operations::shutdown() {
  auto results = drain(OperationCode::cancelled);
  stopped_ = true;
  return results;
}

std::optional<std::chrono::milliseconds> RecoveryWindow::next_delay(Time now,
                                                                  std::uint32_t entropy) noexcept {
  using Ms = std::chrono::milliseconds;
  if (cancelled_ || now >= deadline_) return std::nullopt;
  const unsigned base = std::min(15000u, 1000u << std::min(attempt_, 4u));
  const Ms delay(std::min(15000u, base + entropy % 251));
  // No overflowing time_point addition; a sub-ms remaining TTL is exhausted.
  if (delay >= std::chrono::duration_cast<Ms>(deadline_ - now)) return std::nullopt;
  if (attempt_ < 4) ++attempt_;
  return delay;
}

}  // namespace rtc_client
