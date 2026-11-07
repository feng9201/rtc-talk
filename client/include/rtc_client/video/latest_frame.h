#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <utility>

namespace rtc_client {

enum class FrameOffer { accepted, replaced, stale, closed, invalid };

// Exactly one UNCOMMITTED frame. Destruction is always outside the mutex:
// a pool return callback is permitted to call back into the mailbox.
template <typename T>
class LatestFrameMailbox final {
 public:
  using Frame = std::shared_ptr<const T>;
  explicit LatestFrameMailbox(std::uint64_t generation) : generation_(generation) {}
  LatestFrameMailbox(const LatestFrameMailbox&) = delete;
  LatestFrameMailbox& operator=(const LatestFrameMailbox&) = delete;

  FrameOffer offer(std::uint64_t generation, Frame frame) {
    if (!frame) return FrameOffer::invalid;
    FrameOffer result;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (closed_) return FrameOffer::closed;
      if (generation != generation_) return FrameOffer::stale;
      result = pending_ ? FrameOffer::replaced : FrameOffer::accepted;
      pending_.swap(frame);
    }
    return result;
  }

  Frame take() {
    Frame result;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      pending_.swap(result);
    }
    return result;
  }

  bool advance(std::uint64_t generation) {
    Frame old;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (closed_ || generation <= generation_) return false;
      generation_ = generation;
      pending_.swap(old);
    }
    return true;
  }

  void close() {
    Frame old;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      closed_ = true;
      pending_.swap(old);
    }
  }

 private:
  std::mutex mutex_;
  std::uint64_t generation_;
  bool closed_{false};
  Frame pending_;
};

// Committed leases have a completely different lifecycle from pending frames.
// There is deliberately NO timeout/evict-oldest operation. Compositor release
// is the normal return point; disconnect_confirmed() may only be called after
// destroying the connection so the compositor cannot read those buffers again.
template <typename T>
class InFlightFrames final {
 public:
  using Frame = std::shared_ptr<const T>;
  explicit InFlightFrames(std::size_t limit) : limit_(limit) {}

  bool commit(std::uint64_t buffer_id, Frame frame) {
    if (!frame || buffer_id == 0) return false;
    std::lock_guard<std::mutex> lock(mutex_);
    if (disconnected_ || frames_.size() >= limit_) return false;
    // Keep the parameter's reference until AFTER lock destruction. Even a
    // rejected duplicate emplace may construct/destroy an intermediate node.
    return frames_.emplace(buffer_id, frame).second;
  }

  bool release(std::uint64_t buffer_id) {
    Frame old;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      auto it = frames_.find(buffer_id);
      if (it == frames_.end()) return false;
      old = std::move(it->second);
      frames_.erase(it);
    }
    return true;
  }

  void disconnect_confirmed() {
    std::map<std::uint64_t, Frame> old;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      disconnected_ = true;
      frames_.swap(old);
    }
  }

  std::size_t size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return frames_.size();
  }

 private:
  const std::size_t limit_;
  mutable std::mutex mutex_;
  bool disconnected_{false};
  std::map<std::uint64_t, Frame> frames_;
};

}  // namespace rtc_client
