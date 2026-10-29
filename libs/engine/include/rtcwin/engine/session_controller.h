#pragma once

#include "rtcwin/engine/engine_command.h"
#include "rtcwin/engine/engine_event_sink.h"
#include "rtcwin/engine/media_orchestrator.h"
#include "rtcwin/engine/session_state_machine.h"
#include "rtcwin/engine/signaling_transport.h"

#include <asio.hpp>

#include <functional>
#include <memory>
#include <string>

namespace rtcwin::engine {

// Owns connect/join/leave lifecycle and signaling-side session callbacks.
// Media negotiation is delegated to MediaOrchestrator.
class SessionController final {
 public:
  using TaskPoster = std::function<void(std::function<void()>)>;

  struct Context {
    SignalingTransport& signaling;
    SessionStateMachine& session;
    MediaOrchestrator& media;
    EngineEventSink& events;
    TaskPoster post_to_worker;
  };

  explicit SessionController(Context context);
  ~SessionController();

  SessionController(const SessionController&) = delete;
  SessionController& operator=(const SessionController&) = delete;

  void ConnectAndJoin(const EngineCommand& command);
  void Leave(const EngineCommand& command);
  void Disconnect(const EngineCommand& command);
  void CancelHeartbeat();

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace rtcwin::engine
