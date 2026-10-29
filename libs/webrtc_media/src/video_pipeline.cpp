#include "video_pipeline.h"

#include "media_error.h"

#include "api/make_ref_counted.h"

namespace rtcwin::webrtc_media {

VideoPipeline::VideoPipeline() = default;
VideoPipeline::~VideoPipeline() = default;

bool VideoPipeline::Initialize(
    webrtc::Thread* signaling,
    webrtc::PeerConnectionFactoryInterface* factory, MediaError* error) {
  if (!signaling || !factory) {
    SetError(error, 1707, "创建视频轨失败");
    return false;
  }
  signaling_ = signaling;
  const bool ok = signaling->BlockingCall([this, factory] {
    camera_source_ = webrtc::make_ref_counted<CameraVideoTrackSource>();
    camera_track_ = factory->CreateVideoTrack(camera_source_, "rtcwin-camera");
    if (!camera_track_) return false;
    camera_track_->set_enabled(true);
    return true;
  });
  if (!ok) SetError(error, 1707, "创建视频轨失败");
  return ok;
}

void VideoPipeline::Shutdown() {
  camera_track_ = nullptr;
  camera_source_ = nullptr;
  signaling_ = nullptr;
  camera_enabled_.store(false, std::memory_order_relaxed);
}

bool VideoPipeline::SetEnabled(bool enabled, MediaError* error) {
  if (!camera_track_) {
    SetError(error, 1730, "视频媒体会话未初始化");
    return false;
  }
  camera_enabled_.store(enabled, std::memory_order_relaxed);
  return true;
}

bool VideoPipeline::PushFrame(const VideoFrameData& frame, MediaError* error) {
  if (!camera_source_) {
    SetError(error, 1732, "摄像头视频源未初始化");
    return false;
  }
  if (!camera_enabled_.load(std::memory_order_relaxed)) return true;
  if (!camera_source_->PushFrame(frame)) {
    SetError(error, 1733, "摄像头帧格式无效");
    return false;
  }
  return true;
}

}  // namespace rtcwin::webrtc_media
