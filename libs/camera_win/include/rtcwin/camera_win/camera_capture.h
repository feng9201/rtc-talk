#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace rtcwin::camera_win {

struct CameraDeviceInfo {
  int index = -1;
  std::string name;
};

struct CameraCaptureConfig {
  int device_index = -1;
  int width = 1280;
  int height = 720;
  int fps = 15;
};

struct I420Frame {
  int width = 0;
  int height = 0;
  int stride_y = 0;
  int stride_u = 0;
  int stride_v = 0;
  std::vector<uint8_t> y;
  std::vector<uint8_t> u;
  std::vector<uint8_t> v;
  int64_t timestamp_us = 0;
  std::string device_name;
};

class CameraCaptureSession final {
 public:
  using FrameCallback = std::function<void(const I420Frame&)>;
  using ErrorCallback = std::function<void(const std::string&)>;

  CameraCaptureSession();
  ~CameraCaptureSession();

  CameraCaptureSession(const CameraCaptureSession&) = delete;
  CameraCaptureSession& operator=(const CameraCaptureSession&) = delete;

  bool Start(const CameraCaptureConfig& config, FrameCallback on_frame,
             ErrorCallback on_error, std::string* error);
  void Stop();
  bool running() const;
  std::string device_name() const;
  int width() const;
  int height() const;
  int fps() const;

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

class CameraService final {
 public:
  static std::vector<CameraDeviceInfo> Enumerate();
};

}  // namespace rtcwin::camera_win
