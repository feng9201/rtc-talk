#pragma once

#include "rtcwin/webrtc_media/media_session.h"

#include "media/base/adapted_video_track_source.h"

#include <atomic>
#include <cstdint>

namespace rtcwin::webrtc_media {

// libwebrtc-owned camera source. Frames are pushed by Engine's camera capture
// thread; the class contains no Qt or Windows camera types.
class CameraVideoTrackSource : public webrtc::AdaptedVideoTrackSource {
 public:
  CameraVideoTrackSource() = default;
  ~CameraVideoTrackSource() override = default;

  bool PushFrame(const VideoFrameData& frame);
  std::uint64_t frames() const {
    return frames_.load(std::memory_order_relaxed);
  }

  SourceState state() const override { return SourceState::kLive; }
  bool remote() const override { return false; }
  bool is_screencast() const override { return false; }
  std::optional<bool> needs_denoising() const override { return false; }

 private:
  std::atomic<std::uint64_t> frames_{0};
};

}  // namespace rtcwin::webrtc_media
