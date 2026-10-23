// File: core/media/webrtc/video/camera_video_source.h
// Brief: 基于 Media Foundation 的摄像头视频源。
//
// 只做“采集 + NV12 转 I420”，编码交给 libwebrtc 的 VideoEncoder。
// 线程模型：Start/Stop 在媒体线程调用，采集与送帧都在独立采集线程。
#ifndef TALK_MEDIA_WEBRTC_VIDEO_CAMERA_VIDEO_SOURCE_H_
#define TALK_MEDIA_WEBRTC_VIDEO_CAMERA_VIDEO_SOURCE_H_

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>

#include "media/base/adapted_video_track_source.h"
#include "media/webrtc/video/mf_capture.h"

namespace talk {

class CameraVideoSource : public webrtc::AdaptedVideoTrackSource {
 public:
  // 目标档位；设备不支持时自动退到最接近的分辨率与帧率。
  // device_index < 0 表示使用第一个可用摄像头。
  CameraVideoSource(int max_width, int max_height, int fps,
                    int device_index = -1);
  ~CameraVideoSource() override;

  CameraVideoSource(const CameraVideoSource&) = delete;
  CameraVideoSource& operator=(const CameraVideoSource&) = delete;

  // 失败时 error 带原因；重复调用幂等。
  bool Start(std::string* error);
  void Stop();
  bool running() const { return running_.load(); }

  // 已送出的采集帧数；统计日志用它区分“没出帧”和“出帧但没发出去”。
  uint64_t frames() const { return frames_.load(); }

  // 实际打开的设备名与分辨率（Start 成功后有效）。
  std::string device_name() const;
  std::string last_error() const;

  // 协商后的实际视频参数（Start 成功后有效，用于日志与排障）。
  int video_width() const;
  int video_height() const;
  int video_fps() const;

  // 摄像头不是屏幕共享；未采集期间也必须报 kLive，理由同桌面源。
  SourceState state() const override { return SourceState::kLive; }
  bool remote() const override { return false; }
  bool is_screencast() const override { return false; }
  std::optional<bool> needs_denoising() const override { return false; }

 private:
  void CaptureLoop();
  bool OpenCamera(std::string* error);
  void Deliver();
  void FinishStart(bool ok, std::string error);

  const int max_width_;
  const int max_height_;
  const int fps_;
  const int device_index_;

  // 只在采集线程访问。
  std::unique_ptr<camera::MFAPICapture> capture_;
  std::shared_ptr<uint8_t> nv12_;
  int capture_width_ = 0;
  int capture_height_ = 0;
  int capture_fps_ = 0;
  int64_t next_frame_us_ = 0;

  std::thread thread_;
  std::atomic<bool> running_{false};
  std::atomic<uint64_t> frames_{0};

  mutable std::mutex mutex_;
  std::condition_variable start_cv_;
  bool start_done_ = false;
  bool start_ok_ = false;
  std::string start_error_;
  std::string device_name_;
  std::string last_error_;
  int active_width_ = 0;
  int active_height_ = 0;
  int active_fps_ = 0;
};

}  // namespace talk

#endif  // TALK_MEDIA_WEBRTC_VIDEO_CAMERA_VIDEO_SOURCE_H_
