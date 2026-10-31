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
    screen_source_ =
        webrtc::make_ref_counted<DesktopVideoSource>(1920, 1080, 15);
    screen_track_ = factory->CreateVideoTrack(screen_source_, "rtcwin-screen");
    if (!screen_track_) return false;
    screen_track_->set_enabled(true);
    return true;
  });
  if (!ok) SetError(error, 1707, "创建视频轨失败");
  return ok;
}

void VideoPipeline::Shutdown() {
  if (screen_source_) screen_source_->Stop();
  screen_track_ = nullptr;
  screen_source_ = nullptr;
  camera_track_ = nullptr;
  camera_source_ = nullptr;
  signaling_ = nullptr;
  camera_enabled_.store(false, std::memory_order_relaxed);
  screen_enabled_.store(false, std::memory_order_relaxed);
}

bool VideoPipeline::SetEnabled(bool enabled, MediaError* error) {
  if (!camera_track_) {
    SetError(error, 1730, "视频媒体会话未初始化");
    return false;
  }
  camera_enabled_.store(enabled, std::memory_order_relaxed);
  return true;
}

bool VideoPipeline::SetScreenEnabled(bool enabled, MediaError* error) {
  if (!screen_track_ || !screen_source_) {
    SetError(error, 1734, "屏幕共享源未初始化");
    return false;
  }
  if (enabled) {
    std::string start_error;
    if (!screen_source_->Start(&start_error)) {
      SetError(error, 1735,
               start_error.empty() ? "启动屏幕共享失败" : start_error);
      return false;
    }
    screen_enabled_.store(true, std::memory_order_relaxed);
  } else {
    screen_source_->Stop();
    screen_enabled_.store(false, std::memory_order_relaxed);
  }
  return true;
}

bool VideoPipeline::SetScreenSource(const std::string& source_id, bool window,
                                    MediaError* error) {
  if (!screen_source_) {
    SetError(error, 1734, "屏幕共享源未初始化");
    return false;
  }
  const bool was_enabled = screen_enabled_.load(std::memory_order_relaxed);
  if (was_enabled) screen_source_->Stop();
  screen_source_->SetSource(source_id, window);
  if (was_enabled) {
    std::string start_error;
    if (!screen_source_->Start(&start_error)) {
      screen_enabled_.store(false, std::memory_order_relaxed);
      SetError(error, 1735,
               start_error.empty() ? "切换屏幕共享源失败" : start_error);
      return false;
    }
  }
  screen_enabled_.store(was_enabled, std::memory_order_relaxed);
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

void VideoPipeline::SetScreenFrameCallback(
    std::function<void(const VideoFrameData&)> callback) {
  if (screen_source_) screen_source_->SetFrameCallback(std::move(callback));
}

}  // namespace rtcwin::webrtc_media
