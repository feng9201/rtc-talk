#include "rtcwin/camera_win/camera_capture.h"

#include "mf_capture.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <thread>
#include <utility>

namespace rtcwin::camera_win {
namespace {

talk::camera::FrameSize ChooseSize(const talk::camera::CameraInfo& device,
                                   int width, int height) {
  talk::camera::FrameSize preferred{width, height};
  for (const auto& size : device.supportedFrameSize) {
    if (size.width == preferred.width && size.height == preferred.height) {
      return size;
    }
  }
  const int target_area = width * height;
  talk::camera::FrameSize best{0, 0};
  int best_area = 0;
  for (const auto& size : device.supportedFrameSize) {
    if (size.width <= 0 || size.height <= 0 ||
        (size.width & 1) != 0 || (size.height & 1) != 0) {
      continue;
    }
    const int area = size.width * size.height;
    if (area <= target_area && area > best_area) {
      best = size;
      best_area = area;
    }
  }
  if (best_area > 0) return best;
  if (!device.supportedFrameSize.empty()) return device.supportedFrameSize.front();
  return preferred;
}

int ChooseFps(const talk::camera::CameraInfo& device, int fps) {
  if (device.supportedFps.empty()) return std::max(1, fps);
  int best = *device.supportedFps.begin();
  int best_diff = std::abs(best - fps);
  for (int value : device.supportedFps) {
    const int diff = std::abs(value - fps);
    if (diff < best_diff) {
      best = value;
      best_diff = diff;
    }
  }
  return best;
}

bool ConvertNv12ToI420(const uint8_t* nv12, int width, int height,
                       I420Frame* out) {
  if (nv12 == nullptr || out == nullptr || width <= 0 || height <= 0 ||
      (width & 1) != 0 || (height & 1) != 0) {
    return false;
  }
  const size_t y_size = static_cast<size_t>(width) * height;
  const size_t uv_size = static_cast<size_t>(width / 2) * (height / 2);
  out->width = width;
  out->height = height;
  out->stride_y = width;
  out->stride_u = width / 2;
  out->stride_v = width / 2;
  out->y.resize(y_size);
  out->u.resize(uv_size);
  out->v.resize(uv_size);
  std::memcpy(out->y.data(), nv12, y_size);
  const uint8_t* uv = nv12 + y_size;
  for (int row = 0; row < height / 2; ++row) {
    const uint8_t* src = uv + static_cast<size_t>(row) * width;
    uint8_t* u = out->u.data() + static_cast<size_t>(row) * (width / 2);
    uint8_t* v = out->v.data() + static_cast<size_t>(row) * (width / 2);
    for (int col = 0; col < width / 2; ++col) {
      u[col] = src[col * 2];
      v[col] = src[col * 2 + 1];
    }
  }
  return true;
}

}  // namespace

class CameraCaptureSession::Impl {
 public:
  bool Start(const CameraCaptureConfig& config, FrameCallback on_frame,
             ErrorCallback on_error, std::string* error) {
    Stop();
    {
      std::lock_guard<std::mutex> lock(mutex_);
      config_ = config;
      on_frame_ = std::move(on_frame);
      on_error_ = std::move(on_error);
      start_done_ = false;
      start_ok_ = false;
      start_error_.clear();
      running_ = true;
    }
    thread_ = std::thread(&Impl::CaptureLoop, this);

    std::unique_lock<std::mutex> lock(mutex_);
    if (!start_cv_.wait_for(lock, std::chrono::seconds(6),
                            [this] { return start_done_; })) {
      lock.unlock();
      running_ = false;
      if (thread_.joinable()) thread_.join();
      if (error != nullptr) *error = "camera start timeout";
      return false;
    }
    if (!start_ok_) {
      const std::string reason = start_error_;
      lock.unlock();
      if (thread_.joinable()) thread_.join();
      if (error != nullptr) *error = reason.empty() ? "camera start failed" : reason;
      return false;
    }
    return true;
  }

  void Stop() {
    running_ = false;
    if (thread_.joinable()) thread_.join();
  }

  bool running() const { return running_.load(); }
  std::string device_name() const { std::lock_guard<std::mutex> lock(mutex_); return device_name_; }
  int width() const { std::lock_guard<std::mutex> lock(mutex_); return width_; }
  int height() const { std::lock_guard<std::mutex> lock(mutex_); return height_; }
  int fps() const { std::lock_guard<std::mutex> lock(mutex_); return fps_; }

 private:
  void FinishStart(bool ok, std::string error) {
    {
      std::lock_guard<std::mutex> lock(mutex_);
      start_done_ = true;
      start_ok_ = ok;
      start_error_ = std::move(error);
    }
    start_cv_.notify_all();
  }

  void ReportError(const std::string& error) {
    ErrorCallback callback;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      callback = on_error_;
    }
    if (callback) callback(error);
  }

  void CaptureLoop() {
    const auto devices = talk::camera::MFAPICapture::EnumerateDevices();
    if (devices.empty()) {
      FinishStart(false, "no camera device found");
      running_ = false;
      return;
    }
    auto chosen = devices.begin();
    if (config_.device_index >= 0) {
      chosen = std::find_if(devices.begin(), devices.end(),
                            [this](const talk::camera::CameraInfo& item) {
                              return item.index == config_.device_index;
                            });
      if (chosen == devices.end()) {
        FinishStart(false, "selected camera not found");
        running_ = false;
        return;
      }
    }

    const auto size = ChooseSize(*chosen, config_.width, config_.height);
    const int fps = ChooseFps(*chosen, config_.fps);
    const size_t bytes = static_cast<size_t>(size.width) * size.height * 3 / 2;
    auto nv12 = std::shared_ptr<uint8_t>(new uint8_t[bytes],
                                         std::default_delete<uint8_t[]>());
    auto capture = std::make_unique<talk::camera::MFAPICapture>();
    capture->setBuffer(nv12, bytes);
    std::string init_error;
    if (!capture->init(*chosen, size.width, size.height, fps)) {
      init_error = capture->getCurErr();
      FinishStart(false, init_error.empty() ? "camera init failed" : init_error);
      running_ = false;
      return;
    }

    device_name_ = chosen->name;
    width_ = size.width;
    height_ = size.height;
    fps_ = fps;
    FinishStart(true, {});

    int failures = 0;
    while (running_.load()) {
      if (!capture->doCapture()) {
        if (!running_.load()) break;
        if (++failures >= 30) {
          ReportError(capture->getCurErr());
          break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        continue;
      }
      failures = 0;
      I420Frame frame;
      if (!ConvertNv12ToI420(nv12.get(), width_, height_, &frame)) {
        continue;
      }
      frame.timestamp_us = capture->lastTimestampUs();
      frame.device_name = device_name_;
      FrameCallback callback;
      {
        std::lock_guard<std::mutex> lock(mutex_);
        callback = on_frame_;
      }
      if (callback) callback(frame);
    }

    capture.reset();
    nv12.reset();
    running_ = false;
  }

  mutable std::mutex mutex_;
  std::condition_variable start_cv_;
  CameraCaptureConfig config_;
  FrameCallback on_frame_;
  ErrorCallback on_error_;
  bool start_done_ = false;
  bool start_ok_ = false;
  std::string start_error_;
  std::atomic<bool> running_{false};
  std::thread thread_;
  std::string device_name_;
  int width_ = 0;
  int height_ = 0;
  int fps_ = 0;
};

CameraCaptureSession::CameraCaptureSession() : impl_(std::make_unique<Impl>()) {}
CameraCaptureSession::~CameraCaptureSession() { impl_->Stop(); }

bool CameraCaptureSession::Start(const CameraCaptureConfig& config,
                                 FrameCallback on_frame,
                                 ErrorCallback on_error,
                                 std::string* error) {
  return impl_->Start(config, std::move(on_frame), std::move(on_error), error);
}

void CameraCaptureSession::Stop() { impl_->Stop(); }
std::string CameraCaptureSession::device_name() const { return impl_->device_name(); }
int CameraCaptureSession::width() const { return impl_->width(); }
int CameraCaptureSession::height() const { return impl_->height(); }
int CameraCaptureSession::fps() const { return impl_->fps(); }
bool CameraCaptureSession::running() const { return impl_->running(); }

std::vector<CameraDeviceInfo> CameraService::Enumerate() {
  std::vector<CameraDeviceInfo> result;
  for (const auto& device : talk::camera::MFAPICapture::EnumerateDevices()) {
    result.push_back({device.index, device.name});
  }
  return result;
}

}  // namespace rtcwin::camera_win
