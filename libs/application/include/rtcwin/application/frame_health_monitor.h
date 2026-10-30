#pragma once

#include "rtcwin/domain/types.h"

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace rtcwin::application {

enum class FrameHealthState {
  Waiting,
  Healthy,
  Paused,
  Stale,
  Removed,
};

struct FrameStreamStatus {
  domain::VideoStreamKey key;
  std::string mid;
  bool producer_paused = true;
  std::int64_t last_frame_ms = 0;
  FrameHealthState state = FrameHealthState::Waiting;
};

class FrameHealthMonitor final {
 public:
  static constexpr std::int64_t kStaleAfterMs = 5000;

  void Sync(const domain::SessionSnapshot& snapshot);
  void NoteFrame(const domain::VideoStreamKey& key, const std::string& mid,
                 std::int64_t now_ms);
  void RemoveExcept(const std::vector<domain::VideoStreamKey>& keys);

  FrameHealthState StateFor(const domain::VideoStreamKey& key,
                            std::int64_t now_ms) const;
  std::vector<domain::VideoStreamKey> StaleStreams(
      std::int64_t now_ms) const;
  std::vector<FrameStreamStatus> Statuses(std::int64_t now_ms) const;
  std::size_t size() const { return streams_.size(); }

 private:
  struct Entry {
    domain::VideoStreamKey key;
    std::string mid;
    bool producer_paused = true;
    std::int64_t last_frame_ms = 0;
  };

  std::map<std::string, Entry> streams_;
};

}  // namespace rtcwin::application
