// File: core/media/webrtc/video/desktop_video_source.cpp
// Brief: 实现桌面采集线程、缩放与 I420 送帧。
#include "media/webrtc/video/desktop_video_source.h"

#include <windows.h>

#include <objbase.h>

#include <algorithm>
#include <chrono>
#include <utility>

#include "api/video/i420_buffer.h"
#include "api/video/video_frame.h"
#include "modules/desktop_capture/desktop_capture_options.h"
#include "modules/desktop_capture/desktop_frame.h"
#include "rtc_base/logging.h"
#include "rtc_base/time_utils.h"
#include "third_party/libyuv/include/libyuv/convert_from_argb.h"
#include "third_party/libyuv/include/libyuv/scale_argb.h"

namespace talk {
namespace {

// 采集器回调等不到时最多阻塞这么久，避免 Stop 卡住。
constexpr int kFrameWaitMs = 200;

int AlignEven(int value) {
  return std::max(2, value & ~1);
}

}  // namespace

class DesktopVideoSource::CaptureCallback
    : public webrtc::DesktopCapturer::Callback {
 public:
  explicit CaptureCallback(DesktopVideoSource* owner) : owner_(owner) {}

  void OnCaptureResult(webrtc::DesktopCapturer::Result result,
                       std::unique_ptr<webrtc::DesktopFrame> frame) override {
    if (result != webrtc::DesktopCapturer::Result::SUCCESS || !frame) {
      return;
    }
    owner_->OnDesktopFrame(std::move(frame));
  }

 private:
  DesktopVideoSource* owner_;
};

DesktopVideoSource::DesktopVideoSource(int max_width, int max_height, int fps)
    : max_width_(AlignEven(max_width)),
      max_height_(AlignEven(max_height)),
      fps_(std::clamp(fps, 1, 30)),
      callback_(std::make_unique<CaptureCallback>(this)) {}

DesktopVideoSource::~DesktopVideoSource() {
  Stop();
  callback_.reset();
}

bool DesktopVideoSource::Start(std::string* error) {
  if (running_.load()) {
    return true;
  }
  // WGC/DXGI 采集器与 MF 编码器都要求 COM；MTA 与 WebRTC 线程模型一致。
  const HRESULT hr = ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  if (FAILED(hr) && hr != RPC_E_CHANGED_MODE && hr != S_FALSE) {
    if (error != nullptr) {
      *error = "桌面采集 COM 初始化失败";
    }
    return false;
  }

  webrtc::DesktopCaptureOptions options =
      webrtc::DesktopCaptureOptions::CreateDefault();
  options.set_allow_directx_capturer(true);
  // WGC/DXGI 会把光标画进画面；不支持时退化为无光标。
  options.set_prefer_cursor_embedded(true);
  options.set_detect_updated_region(false);

  auto capturer = webrtc::DesktopCapturer::CreateScreenCapturer(options);
  if (!capturer) {
    if (error != nullptr) {
      *error = "桌面采集器创建失败";
    }
    return false;
  }
  webrtc::DesktopCapturer::SourceList sources;
  if (!capturer->GetSourceList(&sources) || sources.empty()) {
    if (error != nullptr) {
      *error = "未找到可共享的屏幕";
    }
    return false;
  }
  // v1 固定共享主屏；多屏选择留到界面支持后按 Source.id 指定。
  if (!capturer->SelectSource(sources.front().id)) {
    if (error != nullptr) {
      *error = "选择屏幕失败";
    }
    return false;
  }
  capturer->SetMaxFrameRate(static_cast<uint32_t>(fps_));
  capturer->Start(callback_.get());

  {
    std::lock_guard<std::mutex> lock(mutex_);
    capturer_ = std::move(capturer);
    pending_.reset();
  }
  running_ = true;
  thread_ = std::thread(&DesktopVideoSource::CaptureLoop, this);
  RTC_LOG(LS_INFO) << "桌面采集已启动 fps=" << fps_
                   << " max=" << max_width_ << "x" << max_height_
                   << " screen=" << sources.front().title;
  return true;
}

void DesktopVideoSource::Stop() {
  if (!running_.exchange(false)) {
    // 线程可能已经退出但尚未 join。
    if (thread_.joinable()) {
      thread_.join();
    }
  } else {
    frame_ready_.notify_all();
    if (thread_.joinable()) {
      thread_.join();
    }
  }
  std::lock_guard<std::mutex> lock(mutex_);
  pending_.reset();
  if (capturer_) {
    // 采集器析构会停止内部线程；必须晚于采集线程退出。
    capturer_.reset();
    RTC_LOG(LS_INFO) << "桌面采集已停止";
  }
}

webrtc::MediaSourceInterface::SourceState DesktopVideoSource::state() const {
  // 必须始终返回 kLive：发布视频 m-line 时采集还没启动，若报 kEnded，
  // VideoTrack::AddOrUpdateSink 会直接拒绝发送端注册，后续 Start 也救不回来。
  // 未采集期间只是不出帧，语义上等同静音的摄像头。
  return SourceState::kLive;
}

void DesktopVideoSource::OnDesktopFrame(
    std::unique_ptr<webrtc::DesktopFrame> frame) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    pending_ = std::move(frame);
  }
  frame_ready_.notify_all();
}

void DesktopVideoSource::CaptureLoop() {
  const int64_t interval_us = 1000000 / fps_;
  int64_t next_us = webrtc::TimeMicros();
  while (running_.load()) {
    std::unique_ptr<webrtc::DesktopFrame> frame;
    {
      std::unique_lock<std::mutex> lock(mutex_);
      frame = std::move(pending_);
      pending_.reset();
    }
    if (!frame) {
      // 同步采集器会在返回前回调，异步采集器（WGC）由回调唤醒等待。
      capturer_->CaptureFrame();
      std::unique_lock<std::mutex> lock(mutex_);
      frame_ready_.wait_for(lock, std::chrono::milliseconds(kFrameWaitMs),
                            [this] {
                              return pending_ != nullptr || !running_.load();
                            });
      frame = std::move(pending_);
      pending_.reset();
    }
    if (!running_.load()) {
      break;
    }
    if (frame) {
      Deliver(*frame);
    }

    // 按帧间隔节流；采集本身慢于间隔时立即进入下一轮。
    next_us += interval_us;
    const int64_t now_us = webrtc::TimeMicros();
    if (next_us > now_us) {
      std::this_thread::sleep_for(std::chrono::microseconds(next_us - now_us));
    } else {
      next_us = now_us;
    }
  }
}

void DesktopVideoSource::Deliver(const webrtc::DesktopFrame& frame) {
  const int src_width = frame.size().width();
  const int src_height = frame.size().height();
  if (src_width < 2 || src_height < 2 || frame.data() == nullptr) {
    return;
  }

  // 等比缩到配置上限；NV12 编码要求宽高为偶数。
  int width = src_width;
  int height = src_height;
  if (width > max_width_ || height > max_height_) {
    const double scale =
        std::min(static_cast<double>(max_width_) / width,
                 static_cast<double>(max_height_) / height);
    width = static_cast<int>(width * scale);
    height = static_cast<int>(height * scale);
  }
  width = AlignEven(width);
  height = AlignEven(height);

  webrtc::scoped_refptr<webrtc::I420Buffer> buffer =
      webrtc::I420Buffer::Create(width, height);
  if (!buffer) {
    return;
  }

  const uint8_t* src = frame.data();
  int src_stride = frame.stride();
  if (width != src_width || height != src_height) {
    const size_t size = static_cast<size_t>(width) * height * 4;
    if (scaled_bgra_.size() < size) {
      scaled_bgra_.resize(size);
    }
    libyuv::ARGBScale(src, src_stride, src_width, src_height,
                      scaled_bgra_.data(), width * 4, width, height,
                      libyuv::kFilterBox);
    src = scaled_bgra_.data();
    src_stride = width * 4;
  }
  libyuv::ARGBToI420(src, src_stride, buffer->MutableDataY(),
                     buffer->StrideY(), buffer->MutableDataU(),
                     buffer->StrideU(), buffer->MutableDataV(),
                     buffer->StrideV(), width, height);

  webrtc::VideoFrame video =
      webrtc::VideoFrame::Builder()
          .set_video_frame_buffer(buffer)
          .set_timestamp_us(webrtc::TimeMicros())
          .build();
  // AdaptedVideoTrackSource 会把帧广播给所有 sink（编码器）。
  ++frames_;
  OnFrame(video);
}

}  // namespace talk
