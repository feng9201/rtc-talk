// File: core/media/webrtc/video/desktop_video_source.h
// Brief: 基于 libwebrtc DesktopCapturer 的桌面采集视频源。
//
// 只做“采集 + 缩放 + 转 I420”，编码由 MfH264Encoder 经 VideoEncoder 完成。
// 线程模型：Start/Stop 在媒体任务线程调用，帧回调可能来自采集器自己的线程。
#ifndef TALK_MEDIA_WEBRTC_VIDEO_DESKTOP_VIDEO_SOURCE_H_
#define TALK_MEDIA_WEBRTC_VIDEO_DESKTOP_VIDEO_SOURCE_H_

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "media/base/adapted_video_track_source.h"
#include "modules/desktop_capture/desktop_capturer.h"

namespace talk {

class DesktopVideoSource : public webrtc::AdaptedVideoTrackSource {
 public:
  // 画面按最长边等比缩到 max_width/max_height 以内；fps 为采集与编码帧率。
  DesktopVideoSource(int max_width, int max_height, int fps);
  ~DesktopVideoSource() override;

  DesktopVideoSource(const DesktopVideoSource&) = delete;
  DesktopVideoSource& operator=(const DesktopVideoSource&) = delete;

  // 失败时 error 带原因；重复调用幂等。
  bool Start(std::string* error);
  void Stop();
  bool running() const { return running_.load(); }

  // 已送出的采集帧数；统计日志用它区分「没出帧」和「出帧但没发出去」。
  uint64_t frames() const { return frames_.load(); }

  // 桌面共享不是摄像头：不需要降噪，状态随采集线程变化。
  SourceState state() const override;
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
  std::atomic<bool> running_{false};
  std::atomic<uint64_t> frames_{0};
  // 只在采集线程使用的缩放缓冲，避免每帧分配整屏 BGRA。
  std::vector<uint8_t> scaled_bgra_;
};

}  // namespace talk

#endif  // TALK_MEDIA_WEBRTC_VIDEO_DESKTOP_VIDEO_SOURCE_H_
