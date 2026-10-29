#pragma once

#include "rtcwin/camera_win/camera_capture.h"
#include "rtcwin/client_api.h"
#include "rtcwin/signaling/signaling_client.h"

#include <asio.hpp>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <memory>
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
  void HandleAudioDevices(const Command& command);
  void HandleSessionConnectAndJoin(const Command& command);
  void HandleRoomLeave(const Command& command);
  void HandleSessionDisconnect(const Command& command);
  void ScheduleHeartbeat(int seconds);
  void StopSignaling();
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
  asio::io_context signaling_io_;
  std::unique_ptr<asio::executor_work_guard<asio::io_context::executor_type>>
      signaling_work_;
  std::thread signaling_thread_;
  std::unique_ptr<signaling::SignalingClient> signaling_;
  std::atomic<uint64_t> session_command_id_{0};
  std::mutex session_mutex_;
  std::string session_url_;
  std::string session_token_;
  std::string session_uid_;
  std::string session_room_id_;
  std::string session_display_;
  bool session_ready_ = false;
  bool room_joined_ = false;
  int heartbeat_seconds_ = 15;
  asio::steady_timer heartbeat_timer_{signaling_io_};
  camera_win::CameraCaptureSession camera_;
};

}  // namespace rtcwin::engine
