#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>

namespace rtc_client {

struct VideoLimits {
  std::uint32_t width{1920}, height{1080}, fps{30};
  std::size_t streams{1};
  std::uint64_t pixels_per_second{1920ULL * 1080 * 30};
  std::uint64_t bytes{64ULL * 1024 * 1024};
};
struct VideoDemand {
  std::uint32_t width{}, height{}, fps{};
  std::uint64_t pool_bytes{};  // Actual MPP allocation including DPB/inflight.
};
struct VideoUsage {
  std::size_t streams{};
  std::uint64_t pixels_per_second{}, bytes{};
};

class VideoBudget final {
  struct State;
 public:
  class Reservation final {
   public:
    ~Reservation();
    Reservation(const Reservation&) = delete;
    Reservation& operator=(const Reservation&) = delete;
   private:
    friend class VideoBudget;
    Reservation(std::shared_ptr<State>, std::string, std::uint64_t, std::uint64_t);
    std::shared_ptr<State> state_;
    std::string key_;
    std::uint64_t pixels_, bytes_;
    bool active_{false};
  };

  explicit VideoBudget(VideoLimits limits = {});
  // The lease (and its budget) outlives the admission controller if necessary.
  // A resolution change must reserve a new pool before releasing the old one.
  std::shared_ptr<const Reservation> admit(std::string key, VideoDemand demand);
  VideoUsage usage() const;

 private:
  std::shared_ptr<State> state_;
};

}  // namespace rtc_client
