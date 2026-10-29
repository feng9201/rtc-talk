#pragma once

#include "rtcwin/client_api.h"
#include "rtcwin/engine/engine_command.h"
#include "rtcwin/engine/engine_command_router.h"
#include "rtcwin/engine/engine_event_sink.h"
#include "rtcwin/engine/media_orchestrator.h"
#include "rtcwin/engine/session_controller.h"
#include "rtcwin/engine/session_state_machine.h"
#include "rtcwin/engine/signaling_transport.h"

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
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
    signaling::Json data = signaling::Json::object();
    std::string parse_error;
    std::function<void()> task;
  };

  void PostToWorker(std::function<void()> task);
  void PostToSignaling(std::function<void()> task);
  void WorkerLoop();
  void RegisterCommandHandlers();
  void HandleAudioDevices(const EngineCommand& command);

  void EmitResponse(uint64_t id, const std::string& method, bool ok,
                    int error_code, const std::string& message,
                    const signaling::Json& data = signaling::Json::object());
  void EmitNotification(const std::string& method,
                        const signaling::Json& data =
                            signaling::Json::object());

  rtcwin_options options_{};
  EngineEventSink events_;
  EngineCommandRouter command_router_;
  std::string config_json_;
  std::atomic<uint64_t> next_id_{1};
  std::thread worker_;
  std::mutex mutex_;
  std::condition_variable cv_;
  std::deque<Command> commands_;
  bool stopping_ = false;
  SignalingTransport signaling_transport_;
  SessionStateMachine session_state_;
  MediaOrchestrator media_;
  SessionController session_controller_;
};

}  // namespace rtcwin::engine
