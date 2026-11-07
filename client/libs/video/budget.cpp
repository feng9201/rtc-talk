#include "rtc_client/video/budget.h"

#include <mutex>
#include <set>
#include <utility>

namespace rtc_client {

struct VideoBudget::State {
  explicit State(VideoLimits l) : limits(l) {}
  const VideoLimits limits;
  mutable std::mutex mutex;
  VideoUsage usage;
  std::set<std::string> keys;
};

VideoBudget::VideoBudget(VideoLimits limits) : state_(std::make_shared<State>(limits)) {}

VideoBudget::Reservation::Reservation(std::shared_ptr<State> state, std::string key,
                                     std::uint64_t pixels, std::uint64_t bytes)
    : state_(std::move(state)), key_(std::move(key)), pixels_(pixels), bytes_(bytes) {}

VideoBudget::Reservation::~Reservation() {
  if (!active_) return;
  std::lock_guard<std::mutex> lock(state_->mutex);
  state_->keys.erase(key_);
  --state_->usage.streams;
  state_->usage.pixels_per_second -= pixels_;
  state_->usage.bytes -= bytes_;
}

std::shared_ptr<const VideoBudget::Reservation> VideoBudget::admit(std::string key,
                                                                 VideoDemand d) {
  const auto& l = state_->limits;
  if (key.empty() || key.size() > 256 || d.width == 0 || d.height == 0 || d.fps == 0 ||
      d.pool_bytes == 0 || d.width > l.width || d.height > l.height || d.fps > l.fps ||
      d.width > 8192 || d.height > 8192 || d.fps > 240) return {};
  const auto pixels = std::uint64_t{d.width} * d.height * d.fps;
  auto lease = std::shared_ptr<Reservation>(new Reservation(state_, std::move(key), pixels, d.pool_bytes));
  {
    std::lock_guard<std::mutex> lock(state_->mutex);
    const auto& u = state_->usage;
    if (u.streams >= l.streams || pixels > l.pixels_per_second - u.pixels_per_second ||
        d.pool_bytes > l.bytes - u.bytes || !state_->keys.insert(lease->key_).second) return {};
    ++state_->usage.streams;
    state_->usage.pixels_per_second += pixels;
    state_->usage.bytes += d.pool_bytes;
    lease->active_ = true;
  }
  return lease;
}

VideoUsage VideoBudget::usage() const {
  std::lock_guard<std::mutex> lock(state_->mutex);
  return state_->usage;
}

}  // namespace rtc_client
