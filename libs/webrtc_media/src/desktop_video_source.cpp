#include "desktop_video_source.h"

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

namespace rtcwin::webrtc_media {
namespace {

constexpr int kFrameWaitMs = 200;

int AlignEven(int value) { return std::max(2, value & ~1); }

}  // namespace

class DesktopVideoSource::CaptureCallback
    : public webrtc::DesktopCapturer::Callback {
 public:
  explicit CaptureCallback(DesktopVideoSource* owner) : owner_(owner) {}

  void OnCaptureResult(webrtc::DesktopCapturer::Result result,
                       std::unique_ptr<webrtc::DesktopFrame> frame) override {
    if (result != webrtc::DesktopCapturer::Result::SUCCESS || !frame) return;
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
  if (running_.load(std::memory_order_relaxed)) return true;
  const HRESULT hr = ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  if (FAILED(hr) && hr != RPC_E_CHANGED_MODE && hr != S_FALSE) {
    if (error) *error = "桌面采集 COM 初始化失败";
    return false;
  }

  auto options = webrtc::DesktopCaptureOptions::CreateDefault();
  options.set_allow_directx_capturer(true);
  options.set_prefer_cursor_embedded(true);
  options.set_detect_updated_region(false);

  auto capturer = webrtc::DesktopCapturer::CreateScreenCapturer(options);
  if (!capturer) {
    if (error) *error = "桌面采集器创建失败";
    return false;
  }
  webrtc::DesktopCapturer::SourceList sources;
  if (!capturer->GetSourceList(&sources) || sources.empty()) {
    if (error) *error = "未找到可共享的屏幕";
    return false;
  }
  if (!capturer->SelectSource(sources.front().id)) {
    if (error) *error = "选择屏幕失败";
    return false;
  }
  capturer->SetMaxFrameRate(static_cast<std::uint32_t>(fps_));
  capturer->Start(callback_.get());

  {
    std::lock_guard<std::mutex> lock(mutex_);
    capturer_ = std::move(capturer);
    pending_.reset();
  }
  running_.store(true, std::memory_order_relaxed);
  thread_ = std::thread(&DesktopVideoSource::CaptureLoop, this);
  RTC_LOG(LS_INFO) << "screen capture started fps=" << fps_
                   << " max=" << max_width_ << "x" << max_height_
                   << " screen=" << sources.front().title;
  return true;
}

void DesktopVideoSource::Stop() {
  if (!running_.exchange(false, std::memory_order_acq_rel)) {
    if (thread_.joinable()) thread_.join();
  } else {
    frame_ready_.notify_all();
    if (thread_.joinable()) thread_.join();
  }
  std::lock_guard<std::mutex> lock(mutex_);
  pending_.reset();
  capturer_.reset();
}

void DesktopVideoSource::SetFrameCallback(FrameCallback callback) {
  std::lock_guard<std::mutex> lock(mutex_);
  frame_callback_ = std::move(callback);
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
  const std::int64_t interval_us = 1000000 / fps_;
  std::int64_t next_us = webrtc::TimeMicros();
  while (running_.load(std::memory_order_relaxed)) {
    std::unique_ptr<webrtc::DesktopFrame> frame;
    {
      std::unique_lock<std::mutex> lock(mutex_);
      frame = std::move(pending_);
      pending_.reset();
    }
    if (!frame) {
      capturer_->CaptureFrame();
      std::unique_lock<std::mutex> lock(mutex_);
      frame_ready_.wait_for(
          lock, std::chrono::milliseconds(kFrameWaitMs),
          [this] { return pending_ != nullptr || !running_.load(); });
      frame = std::move(pending_);
      pending_.reset();
    }
    if (!running_.load(std::memory_order_relaxed)) break;
    if (frame) Deliver(*frame);

    next_us += interval_us;
    const std::int64_t now_us = webrtc::TimeMicros();
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
  if (src_width < 2 || src_height < 2 || frame.data() == nullptr) return;

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

  auto buffer = webrtc::I420Buffer::Create(width, height);
  if (!buffer) return;

  const std::uint8_t* src = frame.data();
  int src_stride = frame.stride();
  if (width != src_width || height != src_height) {
    const size_t size = static_cast<size_t>(width) * height * 4;
    if (scaled_bgra_.size() < size) scaled_bgra_.resize(size);
    libyuv::ARGBScale(src, src_stride, src_width, src_height,
                      scaled_bgra_.data(), width * 4, width, height,
                      libyuv::kFilterBox);
    src = scaled_bgra_.data();
    src_stride = width * 4;
  }
  libyuv::ARGBToI420(
      src, src_stride, buffer->MutableDataY(), buffer->StrideY(),
      buffer->MutableDataU(), buffer->StrideU(), buffer->MutableDataV(),
      buffer->StrideV(), width, height);

  const std::int64_t timestamp_us = webrtc::TimeMicros();
  FrameCallback callback;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    callback = frame_callback_;
  }
  if (callback) {
    VideoFrameData data;
    data.width = width;
    data.height = height;
    data.stride_y = buffer->StrideY();
    data.stride_u = buffer->StrideU();
    data.stride_v = buffer->StrideV();
    data.data_y = buffer->DataY();
    data.data_u = buffer->DataU();
    data.data_v = buffer->DataV();
    data.timestamp_us = timestamp_us;
    callback(data);
  }

  webrtc::VideoFrame video =
      webrtc::VideoFrame::Builder()
          .set_video_frame_buffer(buffer)
          .set_timestamp_us(timestamp_us)
          .build();
  frames_.fetch_add(1, std::memory_order_relaxed);
  OnFrame(video);
}

}  // namespace rtcwin::webrtc_media
