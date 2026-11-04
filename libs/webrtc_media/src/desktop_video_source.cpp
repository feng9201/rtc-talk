#include "desktop_video_source.h"

#include "rtcwin/log_adapter/log_api.h"
#include "rtcwin/webrtc_media/desktop_capture_service.h"

#include <windows.h>

#include <objbase.h>

#include <algorithm>
#include <chrono>
#include <cstdlib>
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
constexpr auto kCaptureRestartDelay = std::chrono::seconds(2);

class CaptureComApartment {
 public:
  CaptureComApartment() : result_(::CoInitializeEx(nullptr, COINIT_MULTITHREADED)) {}
  ~CaptureComApartment() {
    if (SUCCEEDED(result_)) ::CoUninitialize();
  }
  bool initialized() const { return SUCCEEDED(result_); }

 private:
  HRESULT result_;
};

int AlignEven(int value) { return std::max(2, value & ~1); }

}  // namespace

class DesktopVideoSource::CaptureCallback
    : public webrtc::DesktopCapturer::Callback {
 public:
  explicit CaptureCallback(DesktopVideoSource* owner) : owner_(owner) {}

  void OnCaptureResult(webrtc::DesktopCapturer::Result result,
                       std::unique_ptr<webrtc::DesktopFrame> frame) override {
    owner_->OnCaptureResult(result, std::move(frame));
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
  return Start(source_id_, source_window_, error);
}

bool DesktopVideoSource::Start(const std::string& source_id, bool window,
                               std::string* error) {
  if (running_.load(std::memory_order_relaxed)) return true;
  if (thread_.joinable()) thread_.join();
  source_id_ = source_id;
  source_window_ = window;
  std::promise<std::string> started;
  auto result = started.get_future();
  running_.store(true, std::memory_order_relaxed);
  thread_ = std::thread(&DesktopVideoSource::CaptureLoop, this, source_id,
                        window, std::move(started));
  const auto message = result.get();
  if (message.empty()) return true;
  Stop();
  if (error) *error = message;
  return false;
}

void DesktopVideoSource::CaptureLoop(std::string source_id, bool window,
                                     std::promise<std::string> started) {
  const CaptureComApartment com;
  if (!com.initialized()) {
    started.set_value("桌面采集 COM 初始化失败");
    return;
  }

  auto options = webrtc::DesktopCaptureOptions::CreateDefault();
  options.set_allow_directx_capturer(true);
  options.set_prefer_cursor_embedded(true);
  options.set_detect_updated_region(false);

  const auto create_capturer = [&] {
    return window ? webrtc::DesktopCapturer::CreateWindowCapturer(options)
                  : webrtc::DesktopCapturer::CreateScreenCapturer(options);
  };
  auto capturer = create_capturer();
  if (!capturer) {
    started.set_value(window ? "窗口采集器创建失败" : "桌面采集器创建失败");
    return;
  }
  webrtc::DesktopCapturer::SourceList sources;
  if (!capturer->GetSourceList(&sources) || sources.empty()) {
    started.set_value(window ? "未找到可共享的窗口" : "未找到可共享的屏幕");
    return;
  }
  auto selected = sources.begin();
  if (!source_id.empty()) {
    char* end = nullptr;
    const long long wanted = std::strtoll(source_id.c_str(), &end, 10);
    if (end == source_id.c_str() || *end != '\0') {
      started.set_value("共享源编号无效");
      return;
    }
    selected = std::find_if(
        sources.begin(), sources.end(),
        [wanted](const webrtc::DesktopCapturer::Source& source) {
          return static_cast<long long>(source.id) == wanted;
        });
    if (selected == sources.end()) {
      started.set_value(window ? "窗口共享源不存在" : "屏幕共享源不存在");
      return;
    }
  }
  const auto start_capturer = [&](webrtc::DesktopCapturer* capture) {
    if (capture == nullptr || !capture->SelectSource(selected->id)) return false;
    capture->SetMaxFrameRate(static_cast<std::uint32_t>(fps_));
    capture->Start(callback_.get());
    return true;
  };
  if (!start_capturer(capturer.get())) {
    started.set_value(window ? "选择窗口失败" : "选择屏幕失败");
    return;
  }
  RTC_LOG(LS_INFO) << "screen capture started fps=" << fps_
                   << " max=" << max_width_ << "x" << max_height_
                   << " source=" << selected->title
                   << " type=" << (window ? "window" : "screen");
  started.set_value({});

  const std::int64_t interval_us = 1000000 / fps_;
  std::int64_t next_us = webrtc::TimeMicros();
  auto next_restart = std::chrono::steady_clock::now() + kCaptureRestartDelay;
  bool interrupted = false;
  while (running_.load(std::memory_order_relaxed)) {
    if (capturer) capturer->CaptureFrame();
    std::unique_ptr<webrtc::DesktopFrame> frame;
    std::optional<webrtc::DesktopCapturer::Result> result;
    {
      std::unique_lock<std::mutex> lock(mutex_);
      frame_ready_.wait_for(
          lock, std::chrono::milliseconds(kFrameWaitMs),
          [this] { return capture_result_.has_value() || !running_.load(); });
      frame = std::move(pending_);
      result = std::exchange(capture_result_, std::nullopt);
    }
    if (!running_.load(std::memory_order_relaxed)) break;
    const auto now = std::chrono::steady_clock::now();
    if (frame) {
      Deliver(*frame);
      next_restart = now + kCaptureRestartDelay;
      if (interrupted) {
        rtcwin_log_write(RTCWIN_LOG_INFO, __FILE__, __LINE__, __func__,
                         "screen capture recovered");
      }
      interrupted = false;
    } else if (now >= next_restart) {
      if (!interrupted) {
        const auto message = "screen capture stalled; restarting source id=" +
            std::to_string(selected->id) + " result=" +
            std::to_string(result ? static_cast<int>(*result) : -1);
        rtcwin_log_write(RTCWIN_LOG_WARN, __FILE__, __LINE__, __func__,
                         message.c_str());
      }
      interrupted = true;
      capturer.reset();
      {
        std::lock_guard<std::mutex> lock(mutex_);
        pending_.reset();
        capture_result_.reset();
      }
      capturer = create_capturer();
      if (!start_capturer(capturer.get())) capturer.reset();
      next_restart = now + kCaptureRestartDelay;
    }
    next_us = std::max(next_us + interval_us, webrtc::TimeMicros());
    std::unique_lock<std::mutex> lock(mutex_);
    frame_ready_.wait_for(
        lock, std::chrono::microseconds(next_us - webrtc::TimeMicros()),
        [this] { return !running_.load(std::memory_order_relaxed); });
  }
}

void DesktopVideoSource::SetSource(const std::string& source_id, bool window) {
  source_id_ = source_id;
  source_window_ = window;
}

bool DesktopVideoSource::SetNativeResolution(bool native) {
  if (running_.load(std::memory_order_relaxed)) return false;
  native_resolution_ = native;
  return true;
}

void DesktopVideoSource::Stop() {
  running_.store(false, std::memory_order_relaxed);
  frame_ready_.notify_all();
  if (thread_.joinable()) thread_.join();
  std::lock_guard<std::mutex> lock(mutex_);
  pending_.reset();
  capture_result_.reset();
}

void DesktopVideoSource::SetFrameCallback(FrameCallback callback) {
  std::lock_guard<std::mutex> lock(mutex_);
  frame_callback_ = std::move(callback);
}

void DesktopVideoSource::OnCaptureResult(
    webrtc::DesktopCapturer::Result result,
    std::unique_ptr<webrtc::DesktopFrame> frame) {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    capture_result_ = result;
    pending_ = result == webrtc::DesktopCapturer::Result::SUCCESS
                   ? std::move(frame)
                   : nullptr;
  }
  frame_ready_.notify_all();
}

void DesktopVideoSource::Deliver(const webrtc::DesktopFrame& frame) {
  const int src_width = frame.size().width();
  const int src_height = frame.size().height();
  if (src_width < 2 || src_height < 2 || frame.data() == nullptr) return;

  int width = src_width;
  int height = src_height;
  if (!native_resolution_ && (width > max_width_ || height > max_height_)) {
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

namespace rtcwin::webrtc_media {

bool EnumerateDesktopSources(std::vector<DesktopSourceInfo>* sources,
                             std::string* error) {
  if (sources == nullptr) {
    if (error) *error = "共享源输出为空";
    return false;
  }
  sources->clear();
  auto options = webrtc::DesktopCaptureOptions::CreateDefault();
  options.set_allow_directx_capturer(true);
  options.set_prefer_cursor_embedded(true);
  options.set_detect_updated_region(false);

  const auto append = [sources](webrtc::DesktopCapturer* capturer,
                                DesktopSourceType type) {
    if (capturer == nullptr) return;
    webrtc::DesktopCapturer::SourceList list;
    if (!capturer->GetSourceList(&list)) return;
    for (const auto& source : list) {
      sources->push_back(
          {static_cast<std::int64_t>(source.id), source.title, type});
    }
  };

  auto screens = webrtc::DesktopCapturer::CreateScreenCapturer(options);
  if (!screens) {
    if (error) *error = "桌面采集器创建失败";
    return false;
  }
  append(screens.get(), DesktopSourceType::Screen);
  auto windows = webrtc::DesktopCapturer::CreateWindowCapturer(options);
  append(windows.get(), DesktopSourceType::Window);
  if (sources->empty()) {
    if (error) *error = "未找到可共享的屏幕或窗口";
    return false;
  }
  return true;
}

}  // namespace rtcwin::webrtc_media
