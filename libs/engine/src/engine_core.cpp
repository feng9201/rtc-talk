#include "rtcwin/engine/engine_core.h"

#include "engine_log.h"
#include "rtcwin/webrtc_media/audio_device_service.h"

#include <utility>

namespace rtcwin::engine {

EngineCore::EngineCore(const rtcwin_options& options, std::string config_json)
    : options_(options),
      events_(options_),
      config_json_(std::move(config_json)),
      media_({options_, signaling_transport_, session_state_, events_,
              [this](std::function<void()> task) {
                PostToWorker(std::move(task));
              }}),
      session_controller_(
          {signaling_transport_, session_state_, media_, events_,
           [this](std::function<void()> task) {
             PostToWorker(std::move(task));
           }}) {
  InitializeEngineLogging();
  LOG_INFO("engine starting version={} log={}", "0.1.0", EngineLogPath());
  RegisterCommandHandlers();
  worker_ = std::thread(&EngineCore::WorkerLoop, this);
}

EngineCore::~EngineCore() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (stopping_) return;
    stopping_ = true;
  }
  session_state_.MarkDisconnected();
  signaling_transport_.Stop([this] {
    session_controller_.CancelHeartbeat();
  });
  cv_.notify_all();
  if (worker_.joinable()) worker_.join();
  media_.Reset();
  ShutdownEngineLogging();
}

uint64_t EngineCore::Submit(std::string method, std::string json) {
  constexpr std::size_t kMaxPendingCommands = 256;
  const uint64_t id = next_id_.fetch_add(1);
  Command command;
  command.id = id;
  command.method = std::move(method);
  if (!json.empty()) {
    try {
      command.data = signaling::Json::parse(json);
    } catch (const std::exception& error) {
      command.parse_error = error.what();
    }
  }
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (stopping_ || commands_.size() >= kMaxPendingCommands) return 0;
    commands_.push_back(std::move(command));
  }
  cv_.notify_one();
  return id;
}

void EngineCore::PostToWorker(std::function<void()> task) {
  if (!task) return;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (stopping_ || commands_.size() >= 256) return;
    commands_.push_back(Command{0, {}, {}, {}, std::move(task)});
  }
  cv_.notify_one();
}

void EngineCore::PostToSignaling(std::function<void()> task) {
  signaling_transport_.Post(std::move(task));
}

void EngineCore::WorkerLoop() {
  for (;;) {
    Command command;
    {
      std::unique_lock<std::mutex> lock(mutex_);
      cv_.wait(lock, [this] { return stopping_ || !commands_.empty(); });
      if (stopping_ && commands_.empty()) break;
      command = std::move(commands_.front());
      commands_.pop_front();
    }
    if (command.task) {
      command.task();
    } else if (!command.parse_error.empty()) {
      EmitResponse(command.id, command.method, false, 400,
                   "命令参数不是有效 JSON");
    } else if (!command_router_.Dispatch(
                   EngineCommand{command.id, command.method, command.data})) {
      EmitResponse(command.id, command.method, false, 501, "功能尚未实现");
    }
  }
  media_.Reset();
}

void EngineCore::RegisterCommandHandlers() {
  command_router_.Register("engine.ping", [this](const EngineCommand& command) {
    EmitResponse(command.id, command.method, true, 0, {},
                 signaling::Json::object());
  });
  command_router_.Register(
      "camera.devices", [this](const EngineCommand& command) {
        media_.HandleCameraDevices(command);
      });
  command_router_.Register(
      "camera.preview.start", [this](const EngineCommand& command) {
        media_.HandleCameraPreviewStart(command);
      });
  command_router_.Register(
      "camera.preview.stop", [this](const EngineCommand& command) {
        media_.HandleCameraPreviewStop(command);
      });
  command_router_.Register(
      "audio.devices", [this](const EngineCommand& command) {
        HandleAudioDevices(command);
      });
  command_router_.Register(
      "session.connectAndJoin", [this](const EngineCommand& command) {
        session_controller_.ConnectAndJoin(command);
      });
  command_router_.Register("media.setMute",
                           [this](const EngineCommand& command) {
                             media_.HandleSetMute(command);
                           });
  command_router_.Register("media.toggleCamera",
                           [this](const EngineCommand& command) {
                             media_.HandleToggleCamera(command);
                           });
  command_router_.Register(
      "media.toggleScreenShare", [this](const EngineCommand& command) {
        media_.HandleToggleScreenShare(command);
      });
  command_router_.Register("media.setVolume",
                           [this](const EngineCommand& command) {
                             media_.HandleSetVolume(command);
                           });
  command_router_.Register("media.setDeafened",
                           [this](const EngineCommand& command) {
                             media_.HandleSetDeafened(command);
                           });
  command_router_.Register("media.stats",
                           [this](const EngineCommand& command) {
                             media_.HandleStats(command);
                           });
  command_router_.Register("room.leave",
                           [this](const EngineCommand& command) {
                             session_controller_.Leave(command);
                           });
  command_router_.Register("session.disconnect",
                           [this](const EngineCommand& command) {
                             session_controller_.Disconnect(command);
                           });
}

void EngineCore::HandleAudioDevices(const EngineCommand& command) {
  webrtc_media::AudioDeviceList devices;
  std::string error;
  if (!webrtc_media::EnumerateAudioDevices(&devices, &error)) {
    EmitResponse(command.id, command.method, false, 1601,
                 error.empty() ? "音频设备枚举失败" : error);
    return;
  }
  signaling::Json data = {{"recording", signaling::Json::array()},
                          {"playout", signaling::Json::array()}};
  const auto append = [&data](
                          const char* key,
                          const std::vector<webrtc_media::AudioDeviceInfo>&
                              items) {
    for (const auto& device : items) {
      data[key].push_back(
          {{"index", device.index}, {"id", device.id}, {"name", device.name}});
    }
  };
  append("recording", devices.recording);
  append("playout", devices.playout);
  EmitResponse(command.id, command.method, true, 0, {}, data);
}

void EngineCore::EmitResponse(uint64_t id, const std::string& method, bool ok,
                              int error_code, const std::string& message,
                              const signaling::Json& data) {
  events_.EmitResponse(id, method, ok, error_code, message, data);
}

void EngineCore::EmitNotification(const std::string& method,
                                  const signaling::Json& data) {
  events_.EmitNotification(method, data);
}

}  // namespace rtcwin::engine
