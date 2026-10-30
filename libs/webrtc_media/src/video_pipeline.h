#pragma once
/**
 * @brief:摄像头视频源、视频轨、帧推送
*/
#include "rtcwin/webrtc_media/media_session.h"

#include "api/media_stream_interface.h"
#include "api/peer_connection_interface.h"
#include "rtc_base/thread.h"
#include "video_track_source.h"

#include <atomic>
#include <cstdint>

namespace rtcwin::webrtc_media {

class VideoPipeline final {
 public:
  VideoPipeline();
  ~VideoPipeline();

  VideoPipeline(const VideoPipeline&) = delete;
  VideoPipeline& operator=(const VideoPipeline&) = delete;

  bool Initialize(webrtc::Thread* signaling,
                  webrtc::PeerConnectionFactoryInterface* factory,
                  MediaError* error);
  void Shutdown();
  bool SetEnabled(bool enabled, MediaError* error);
  bool PushFrame(const VideoFrameData& frame, MediaError* error);

  webrtc::scoped_refptr<webrtc::VideoTrackInterface> track() const {
    return camera_track_;
  }
  bool enabled() const { return camera_enabled_.load(std::memory_order_relaxed); }
  std::uint64_t frames() const {
    return camera_source_ ? camera_source_->frames() : 0;
  }

 private:
  webrtc::Thread* signaling_ = nullptr;
  webrtc::scoped_refptr<CameraVideoTrackSource> camera_source_;
  webrtc::scoped_refptr<webrtc::VideoTrackInterface> camera_track_;
  std::atomic<bool> camera_enabled_{false};
};

}  // namespace rtcwin::webrtc_media
