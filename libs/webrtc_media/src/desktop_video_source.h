#pragma once

#include "rtcwin/webrtc_media/media_session.h"

#include "media/base/adapted_video_track_source.h"
#include "modules/desktop_capture/desktop_capturer.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace rtcwin::webrtc_media {

// Desktop capture source. WGC/DXGI capture and BGRA -> I420 conversion live
// here; WebRTC owns the resulting video track.
class DesktopVideoSource : public webrtc::AdaptedVideoTrackSource {
 public:
  using FrameCallback = std::function<void(const VideoFrameData&)>;

  DesktopVideoSource(int max_width, int max_height, int fps);
  ~DesktopVideoSource() override;

  DesktopVideoSource(const DesktopVideoSource&) = delete;
  DesktopVideoSource& operator=(const DesktopVideoSource&) = delete;

  bool Start(std::string* error);
  bool Start(const std::string& source_id, bool window,
             std::string* error);
  void SetSource(const std::string& source_id, bool window);
  void Stop();
  bool running() const { return running_.load(std::memory_order_relaxed); }
  std::uint64_t frames() const {
    return frames_.load(std::memory_order_relaxed);
  }
  void SetFrameCallback(FrameCallback callback);

  SourceState state() const override { return SourceState::kLive; }
  bool remote() const override { return false; }
  bool is_screencast() const override { return true; }
  std::optional<bool> needs_denoising() const override { return false; }

 private:
  class CaptureCallback;

  void CaptureLoop();
  void Deliver(const webrtc::DesktopFrame& frame);
  void OnDesktopFrame(std::unique_ptr<webrtc::DesktopFrame> frame);

  const int max_width_;
  const int max_height_;
  const int fps_;

  std::mutex mutex_;
  std::condition_variable frame_ready_;
  std::unique_ptr<webrtc::DesktopFrame> pending_;
  std::unique_ptr<webrtc::DesktopCapturer> capturer_;
  std::unique_ptr<CaptureCallback> callback_;
  std::thread thread_;
  FrameCallback frame_callback_;
  std::atomic<bool> running_{false};
  std::atomic<std::uint64_t> frames_{0};
  std::vector<std::uint8_t> scaled_bgra_;
  std::string source_id_;
  bool source_window_ = false;
};

}  // namespace rtcwin::webrtc_media
