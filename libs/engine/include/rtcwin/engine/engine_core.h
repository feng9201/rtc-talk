#pragma once

#include "rtcwin/camera_win/camera_capture.h"
#include "rtcwin/client_api.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <thread>

namespace rtcwin::engine {

class EngineCore final {
 public:
  EngineCore(const rtcwin_options& options, std::string config_json);
  ~EngineCore();

  EngineCore(const EngineCore&) = delete;
  EngineCore& operator=(const EngineCore&) = delete;

  uint64_t Submit(std::string method, std::string json);

 private:
  struct Command {
    uint64_t id = 0;
    std::string method;
    std::string json;
  };

  void WorkerLoop();
  void HandleCommand(const Command& command);
  void HandleCameraDevices(const Command& command);
  void HandleCameraPreviewStart(const Command& command);
  void HandleCameraPreviewStop(const Command& command);
  void OnCameraFrame(const camera_win::I420Frame& frame);
  void EmitResponse(uint64_t id, const std::string& method, bool ok,
                    int error_code, const std::string& message,
                    const std::string& data_json = "{}");
  void EmitNotification(const std::string& method,
                        const std::string& data_json);
  static int ParseIntField(const std::string& json, const std::string& name,
                           int fallback);

  rtcwin_options options_{};
  std::string config_json_;
  std::atomic<uint64_t> next_id_{1};
  std::thread worker_;
  std::mutex mutex_;
  std::condition_variable cv_;
  std::deque<Command> commands_;
  bool stopping_ = false;
  camera_win::CameraCaptureSession camera_;
};

}  // namespace rtcwin::engine
