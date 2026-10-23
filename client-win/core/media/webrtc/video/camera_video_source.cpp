// File: core/media/webrtc/video/camera_video_source.cpp
// Brief: 摄像头采集线程、分辨率/帧率协商与 NV12→I420 送帧。
#include "media/webrtc/video/camera_video_source.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <utility>

#include "api/video/i420_buffer.h"
#include "api/video/video_frame.h"
#include "rtc_base/logging.h"
#include "rtc_base/time_utils.h"
#include "third_party/libyuv/include/libyuv/convert.h"

namespace talk {
namespace {

// Start 等待采集线程打开设备的超时；部分 USB 相机初始化要 1~2 秒。
constexpr int kStartTimeoutMs = 5000;
// 连续失败这么多次就退出采集线程，避免设备被拔掉后空转刷日志。
constexpr int kMaxConsecutiveFailures = 30;

camera::FrameSize PickFrameSize(const camera::CameraInfo& info, int max_width,
                                int max_height) {
  camera::FrameSize exact{max_width, max_height};
  for (const auto& size : info.supportedFrameSize) {
    if (size.width == exact.width && size.height == exact.height) {
      return size;
    }
  }

  // 优先不超过目标档位的最大分辨率；都超过时取面积最小的。
  const int target_area = max_width * max_height;
  bool found = false;
  camera::FrameSize best{0, 0};
  int best_area = 0;
  for (const auto& size : info.supportedFrameSize) {
    if (size.width <= 0 || size.height <= 0) {
      continue;
    }
    if ((size.width & 1) != 0 || (size.height & 1) != 0) {
      continue;  // I420 要求偶数宽高
    }
    const int area = size.width * size.height;
    if (area <= target_area && area > best_area) {
      best = size;
      best_area = area;
      found = true;
    }
  }
  if (found) {
    return best;
  }

  int smallest_area = 0;
  for (const auto& size : info.supportedFrameSize) {
    if (size.width <= 0 || size.height <= 0 ||
        (size.width & 1) != 0 || (size.height & 1) != 0) {
      continue;
    }
    const int area = size.width * size.height;
    if (smallest_area == 0 || area < smallest_area) {
      best = size;
      smallest_area = area;
    }
  }
  return best;
}

int PickFps(const camera::CameraInfo& info, int target) {
  if (info.supportedFps.empty()) {
    return target;
  }
  // 优先精确命中；否则取不超过 30 且最接近目标的值。
  int best = 0;
  for (int fps : info.supportedFps) {
    if (fps == target) {
      return fps;
    }
    if (fps > 30) {
      continue;
    }
    if (best == 0 || std::abs(fps - target) < std::abs(best - target)) {
      best = fps;
    }
  }
  return best > 0 ? best : *info.supportedFps.begin();
}

}  // namespace

CameraVideoSource::CameraVideoSource(int max_width, int max_height, int fps,
                                     int device_index)
    : max_width_(std::max(2, max_width & ~1)),
      max_height_(std::max(2, max_height & ~1)),
      fps_(std::clamp(fps, 1, 30)),
      device_index_(device_index) {}

CameraVideoSource::~CameraVideoSource() {
  Stop();
}

bool CameraVideoSource::Start(std::string* error) {
  if (running_.load()) {
    return true;
  }
  {
    std::lock_guard<std::mutex> lock(mutex_);
    start_done_ = false;
    start_ok_ = false;
    start_error_.clear();
    last_error_.clear();
  }

  running_ = true;
  thread_ = std::thread(&CameraVideoSource::CaptureLoop, this);

  std::unique_lock<std::mutex> lock(mutex_);
  if (!start_cv_.wait_for(lock, std::chrono::milliseconds(kStartTimeoutMs),
                          [this] { return start_done_; })) {
    lock.unlock();
    running_ = false;
    if (thread_.joinable()) {
      thread_.join();
    }
    if (error != nullptr) {
      *error = "摄像头启动超时";
    }
    return false;
  }
  if (!start_ok_) {
    const std::string reason = start_error_;
    lock.unlock();
    running_ = false;
    if (thread_.joinable()) {
      thread_.join();
    }
    if (error != nullptr) {
      *error = reason.empty() ? "摄像头启动失败" : reason;
    }
    return false;
  }
  return true;
}

void CameraVideoSource::Stop() {
  running_ = false;
  if (thread_.joinable()) {
    thread_.join();
  }
  std::lock_guard<std::mutex> lock(mutex_);
  start_done_ = false;
  start_ok_ = false;
}

std::string CameraVideoSource::device_name() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return device_name_;
}

std::string CameraVideoSource::last_error() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return last_error_;
}

int CameraVideoSource::video_width() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return active_width_;
}

int CameraVideoSource::video_height() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return active_height_;
}

int CameraVideoSource::video_fps() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return active_fps_;
}

void CameraVideoSource::FinishStart(bool ok, std::string error) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    start_done_ = true;
    start_ok_ = ok;
    start_error_ = std::move(error);
  }
  start_cv_.notify_all();
}

bool CameraVideoSource::OpenCamera(std::string* error) {
  const auto devices = camera::MFAPICapture::EnumerateDevices();
  if (devices.empty()) {
    if (error != nullptr) {
      *error = "未找到摄像头设备";
    }
    return false;
  }

  auto chosen = devices.end();
  if (device_index_ >= 0) {
    for (auto it = devices.begin(); it != devices.end(); ++it) {
      if (it->index == device_index_) {
        chosen = it;
        break;
      }
    }
  } else {
    // 默认取第一个能报出分辨率的设备（被占用的设备可能只有名字）。
    for (auto it = devices.begin(); it != devices.end(); ++it) {
      if (!it->supportedFrameSize.empty()) {
        chosen = it;
        break;
      }
    }
    if (chosen == devices.end()) {
      chosen = devices.begin();
    }
  }
  if (chosen == devices.end()) {
    if (error != nullptr) {
      *error = "未找到指定摄像头 index=" + std::to_string(device_index_);
    }
    return false;
  }

  const camera::FrameSize size = PickFrameSize(*chosen, max_width_, max_height_);
  if (size.width <= 0 || size.height <= 0) {
    if (error != nullptr) {
      *error = "摄像头没有可用的视频格式";
    }
    return false;
  }
  capture_fps_ = PickFps(*chosen, fps_);

  capture_ = std::make_unique<camera::MFAPICapture>();
  const size_t frame_bytes = static_cast<size_t>(size.width) * size.height * 3 / 2;
  nv12_ = std::shared_ptr<uint8_t>(new uint8_t[frame_bytes],
                                   std::default_delete<uint8_t[]>());
  capture_->setBuffer(nv12_, frame_bytes);
  if (!capture_->init(*chosen, size.width, size.height, capture_fps_)) {
    if (error != nullptr) {
      *error = capture_->getCurErr();
    }
    capture_.reset();
    nv12_.reset();
    return false;
  }

  capture_width_ = capture_->width();
  capture_height_ = capture_->height();
  {
    std::lock_guard<std::mutex> lock(mutex_);
    device_name_ = chosen->name;
    active_width_ = capture_width_;
    active_height_ = capture_height_;
    active_fps_ = capture_fps_;
  }
  RTC_LOG(LS_INFO) << "摄像头已启动 name=" << chosen->name
                   << " size=" << capture_width_ << "x" << capture_height_
                   << " fps=" << capture_fps_ << " device=" << chosen->index;
  return true;
}

void CameraVideoSource::CaptureLoop() {
  std::string error;
  if (!OpenCamera(&error)) {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      last_error_ = error;
    }
    FinishStart(false, error);
    running_ = false;
    return;
  }
  FinishStart(true, std::string());

  // 部分驱动会忽略协商的帧率（实测 C925e 标 15fps 实际按 ~60fps 出帧），
  // 所以无论协商值多少都要按目标帧率节流。留 1/5 间隔的容差：设备本来
  // 就跑在目标帧率时，因到达抖动被判成“提前”而误丢，曾实测掉到 ~12fps。
  const int64_t interval_us = 1000000 / std::max(1, fps_);
  const int64_t early_tolerance_us = interval_us / 5;
  int failures = 0;
  while (running_.load()) {
    if (!capture_->doCapture()) {
      if (!running_.load()) {
        break;
      }
      if (++failures >= kMaxConsecutiveFailures) {
        const std::string reason = capture_->getCurErr();
        {
          std::lock_guard<std::mutex> lock(mutex_);
          last_error_ = reason;
        }
        RTC_LOG(LS_WARNING) << "摄像头连续采集失败，停止："
                            << (reason.empty() ? "unknown" : reason);
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
      continue;
    }
    failures = 0;

    // 设备按自己的帧率出帧，高于目标时在这里丢帧，避免多做一次 I420 转换。
    const int64_t now_us = webrtc::TimeMicros();
    if (next_frame_us_ != 0 &&
        now_us + early_tolerance_us < next_frame_us_) {
      continue;
    }
    next_frame_us_ = now_us + interval_us;
    Deliver();
  }

  // COM / MF 的成对释放留在采集线程，避免跨套间析构。
  capture_.reset();
  nv12_.reset();
  running_ = false;
  RTC_LOG(LS_INFO) << "摄像头采集已停止";
}

void CameraVideoSource::Deliver() {
  if (!capture_ || !nv12_ || capture_width_ <= 0 || capture_height_ <= 0) {
    return;
  }
  const int width = capture_width_;
  const int height = capture_height_;
  const size_t y_size = static_cast<size_t>(width) * height;

  webrtc::scoped_refptr<webrtc::I420Buffer> buffer =
      webrtc::I420Buffer::Create(width, height);
  if (!buffer) {
    return;
  }
  libyuv::NV12ToI420(nv12_.get(), width, nv12_.get() + y_size, width,
                     buffer->MutableDataY(), buffer->StrideY(),
                     buffer->MutableDataU(), buffer->StrideU(),
                     buffer->MutableDataV(), buffer->StrideV(), width, height);

  webrtc::VideoFrame frame = webrtc::VideoFrame::Builder()
                                 .set_video_frame_buffer(buffer)
                                 .set_timestamp_us(webrtc::TimeMicros())
                                 .build();
  // AdaptedVideoTrackSource 会把帧广播给所有 sink（编码器）。
  ++frames_;
  OnFrame(frame);
}

}  // namespace talk
