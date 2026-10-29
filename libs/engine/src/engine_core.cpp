#include "rtcwin/engine/engine_core.h"

#include "rtcwin/signaling/protocol.h"
#include "rtcwin/webrtc_media/audio_device_service.h"

#include <algorithm>
#include <chrono>
#include <future>
#include <sstream>
#include <utility>

namespace rtcwin::engine {
namespace {

using signaling::Json;

std::string EscapeJson(const std::string& value) {
  std::string result;
  result.reserve(value.size() + 8);
  for (char ch : value) {
    switch (ch) {
      case '"': result += "\\\""; break;
      case '\\': result += "\\\\"; break;
      case '\n': result += "\\n"; break;
      case '\r': result += "\\r"; break;
      case '\t': result += "\\t"; break;
      default: result += ch; break;
    }
  }
  return result;
}

int ResponseCode(const Json& result) {
  return result.value("error", Json::object()).value("code", 500);
}

std::string ResponseMessage(const Json& result) {
  return result.value("error", Json::object())
      .value("msg", std::string("信令请求失败"));
}

std::string ErrorJson(int code, const std::string& message) {
  return std::string("{\"code\":") + std::to_string(code) +
         ",\"msg\":\"" + EscapeJson(message) + "\"}";
}

}  // namespace

EngineCore::EngineCore(const rtcwin_options& options, std::string config_json)
    : options_(options), config_json_(std::move(config_json)) {
  worker_ = std::thread(&EngineCore::WorkerLoop, this);
  signaling_work_ = std::make_unique<
      asio::executor_work_guard<asio::io_context::executor_type>>(
      asio::make_work_guard(signaling_io_));
  signaling_thread_ = std::thread([this] {
    signaling_ = std::make_unique<signaling::SignalingClient>(signaling_io_);
    signaling_io_.run();
  });
}

EngineCore::~EngineCore() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    stopping_ = true;
  }
  cv_.notify_all();
  if (worker_.joinable()) worker_.join();
  StopSignaling();
  camera_.Stop();
}

uint64_t EngineCore::Submit(std::string method, std::string json) {
  constexpr std::size_t kMaxPendingCommands = 256;
  const uint64_t id = next_id_.fetch_add(1);
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (stopping_ || commands_.size() >= kMaxPendingCommands) return 0;
    commands_.push_back(Command{id, std::move(method), std::move(json)});
  }
  cv_.notify_one();
  return id;
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
    HandleCommand(command);
  }
  camera_.Stop();
}

void EngineCore::HandleCommand(const Command& command) {
  if (command.method == "engine.ping") {
    EmitResponse(command.id, command.method, true, 0, {}, "{}");
  } else if (command.method == "camera.devices") {
    HandleCameraDevices(command);
  } else if (command.method == "camera.preview.start") {
    HandleCameraPreviewStart(command);
  } else if (command.method == "camera.preview.stop") {
    HandleCameraPreviewStop(command);
  } else if (command.method == "audio.devices") {
    HandleAudioDevices(command);
  } else if (command.method == "session.connectAndJoin") {
    HandleSessionConnectAndJoin(command);
  } else if (command.method == "room.leave") {
    HandleRoomLeave(command);
  } else if (command.method == "session.disconnect") {
    HandleSessionDisconnect(command);
  } else {
    EmitResponse(command.id, command.method, false, 501,
                 "功能尚未实现");
  }
}

void EngineCore::HandleCameraDevices(const Command& command) {
  const auto devices = camera_win::CameraService::Enumerate();
  std::ostringstream data;
  data << "{\"cameras\":[";
  bool first = true;
  for (const auto& device : devices) {
    if (!first) data << ',';
    first = false;
    data << "{\"id\":\"" << device.index << "\",\"name\":\""
         << EscapeJson(device.name) << "\"}";
  }
  data << "]}";
  EmitResponse(command.id, command.method, true, 0, {}, data.str());
}

void EngineCore::HandleCameraPreviewStart(const Command& command) {
  camera_.Stop();
  camera_win::CameraCaptureConfig config;
  config.device_index = ParseIntField(command.json, "device_index", -1);
  std::string error;
  const bool started = camera_.Start(
      config,
      [this](const camera_win::I420Frame& frame) { OnCameraFrame(frame); },
      [this](const std::string& message) {
        EmitNotification("camera.preview.error",
                         std::string("{\"message\":\"") +
                             EscapeJson(message) + "\"}");
      },
      &error);
  if (!started) {
    EmitResponse(command.id, command.method, false, 1501,
                 error.empty() ? "camera start failed" : error);
    return;
  }
  std::ostringstream data;
  data << "{\"device_index\":" << config.device_index
       << ",\"device\":\"" << EscapeJson(camera_.device_name())
       << "\",\"width\":" << camera_.width()
       << ",\"height\":" << camera_.height()
       << ",\"fps\":" << camera_.fps() << "}";
  EmitResponse(command.id, command.method, true, 0, {}, data.str());
}

void EngineCore::HandleCameraPreviewStop(const Command& command) {
  camera_.Stop();
  EmitResponse(command.id, command.method, true, 0, {}, "{}");
}

void EngineCore::HandleAudioDevices(const Command& command) {
  webrtc_media::AudioDeviceList devices;
  std::string error;
  if (!webrtc_media::EnumerateAudioDevices(&devices, &error)) {
    EmitResponse(command.id, command.method, false, 1601,
                 error.empty() ? "音频设备枚举失败" : error);
    return;
  }
  Json data = {{"recording", Json::array()}, {"playout", Json::array()}};
  const auto append = [&data](const char* key,
                              const std::vector<webrtc_media::AudioDeviceInfo>& items) {
    for (const auto& device : items) {
      data[key].push_back(
          {{"index", device.index}, {"id", device.id}, {"name", device.name}});
    }
  };
  append("recording", devices.recording);
  append("playout", devices.playout);
  EmitResponse(command.id, command.method, true, 0, {}, data.dump());
}
void EngineCore::HandleSessionConnectAndJoin(const Command& command) {
  Json request;
  try {
    request = Json::parse(command.json);
  } catch (const std::exception&) {
    EmitResponse(command.id, command.method, false, 400, "连接参数不是有效 JSON");
    return;
  }

  const std::string url = request.value("url", std::string());
  const std::string token = request.value("token", std::string());
  const std::string uid = request.value("user_id", std::string());
  const std::string room_id = request.value("room_id", std::string());
  const std::string display = request.value("display", uid);
  if (url.rfind("ws://", 0) != 0 || token.empty() || uid.empty() ||
      room_id.empty()) {
    EmitResponse(command.id, command.method, false, 400,
                 "信令地址、Token、用户标识和房间 ID 不能为空");
    return;
  }

  {
    std::lock_guard<std::mutex> lock(session_mutex_);
    session_url_ = url;
    session_token_ = token;
    session_uid_ = uid;
    session_room_id_ = room_id;
    session_display_ = display;
    session_ready_ = false;
    room_joined_ = false;
  }
  session_command_id_.store(command.id);
  EmitNotification("session.state", "{\"state\":\"connecting\"}");

  asio::post(signaling_io_, [this, url, token, uid, room_id, display] {
    if (!signaling_) {
      const uint64_t id = session_command_id_.exchange(0);
      if (id != 0) {
        EmitResponse(id, "session.connectAndJoin", false, 500,
                     "信令线程尚未初始化");
      }
      return;
    }

    const auto fail_connect = [this](int code, const std::string& message) {
      const uint64_t id = session_command_id_.exchange(0);
      if (id != 0) {
        EmitResponse(id, "session.connectAndJoin", false, code, message);
      }
      std::lock_guard<std::mutex> lock(session_mutex_);
      session_ready_ = false;
      room_joined_ = false;
    };

    signaling::SignalingClient::Handlers handlers;
    handlers.on_open = [this] {
      Json hello = {
          {"client",
           {{"platform", "windows-x64"},
            {"version", "0.1.0"},
            {"sdk", "rtcwin-s2"}}},
          {"caps",
           {{"audio", {{"codecs", Json::array({"opus"})}, {"aec", "webrtc"}}},
            {"p2p", false},
            {"p2p_video", false},
            {"sfu_sdp", "unified-plan"}}}};
      signaling_->Request("session.hello", hello, [this](Json result) {
        if (!result.value("ok", false)) {
          const uint64_t id = session_command_id_.exchange(0);
          if (id != 0) {
            EmitResponse(id, "session.connectAndJoin", false,
                         ResponseCode(result), ResponseMessage(result));
          }
          return;
        }
        {
          std::lock_guard<std::mutex> lock(session_mutex_);
          session_ready_ = true;
        }
        ScheduleHeartbeat(result.value("data", Json::object())
                              .value("heartbeat_interval", 15));
        std::string room_id;
        std::string uid;
        std::string display;
        {
          std::lock_guard<std::mutex> lock(session_mutex_);
          room_id = session_room_id_;
          uid = session_uid_;
          display = session_display_;
        }
        Json join = {
            {"room_id", room_id},
            {"display", display.empty() ? uid : display},
            {"media",
             {{"audio", {{"send", true}, {"muted", true}}},
              {"video", {{"send", false}}}}},
            {"policy", {{"talk_mode", "free"}, {"media_mode", "sfu"}}}};
        signaling_->Request("room.join", join, [this](Json joined) {
          if (!joined.value("ok", false)) {
            const uint64_t id = session_command_id_.exchange(0);
            if (id != 0) {
              EmitResponse(id, "session.connectAndJoin", false,
                           ResponseCode(joined), ResponseMessage(joined));
            }
            std::lock_guard<std::mutex> lock(session_mutex_);
            room_joined_ = false;
            return;
          }
          {
            std::lock_guard<std::mutex> lock(session_mutex_);
            room_joined_ = true;
          }
          const Json data = joined.value("data", Json::object());
          const uint64_t id = session_command_id_.exchange(0);
          if (id != 0) {
            EmitResponse(id, "session.connectAndJoin", true, 0, {},
                         data.dump());
          }
          EmitNotification("session.ready", data.dump());
        });
      });
    };
    handlers.on_message = [this](Json message) {
      const std::string type = message.value("type", std::string());
      if (type == "req") {
        const std::string request_id = message.value("id", std::string());
        signaling_->Send(signaling::Failure(
            request_id, 501, "媒体切换尚未实现"));
        return;
      }
      const std::string method = message.value("method", std::string());
      if (!method.empty() && message.contains("data")) {
        EmitNotification(method, message["data"].dump());
      }
    };
    handlers.on_closed = [this](const signaling::SignalingClient::CloseInfo& info) {
      const uint64_t id = session_command_id_.exchange(0);
      if (id != 0) {
        EmitResponse(id, "session.connectAndJoin", false,
                     info.status != 0 ? info.status : 480, "信令连接关闭");
      }
      {
        std::lock_guard<std::mutex> lock(session_mutex_);
        session_ready_ = false;
        room_joined_ = false;
      }
      heartbeat_timer_.cancel();
      std::ostringstream data;
      data << "{\"handshake_failed\":" << (info.handshake_failed ? "true" : "false")
           << ",\"fatal\":" << (info.fatal ? "true" : "false")
           << ",\"status\":" << info.status << ",\"code\":" << info.code << "}";
      EmitNotification("session.closed", data.str());
    };
    handlers.on_error = [this](int code, std::string message) {
      std::ostringstream data;
      data << "{\"code\":" << code << ",\"message\":\""
           << EscapeJson(message) << "\"}";
      EmitNotification("session.error", data.str());
    };

    if (!signaling_->Connect(url, token, std::move(handlers))) {
      fail_connect(400, "信令地址无效");
    }
  });
}

void EngineCore::HandleRoomLeave(const Command& command) {
  std::string room_id;
  bool joined = false;
  {
    std::lock_guard<std::mutex> lock(session_mutex_);
    room_id = session_room_id_;
    joined = room_joined_;
  }
  if (!joined || room_id.empty()) {
    EmitResponse(command.id, command.method, true, 0, {}, "{}");
    return;
  }
  const uint64_t id = command.id;
  asio::post(signaling_io_, [this, id, room_id] {
    if (!signaling_) {
      EmitResponse(id, "room.leave", false, 500, "信令线程尚未初始化");
      return;
    }
    signaling_->Request("room.leave", {{"room_id", room_id}}, [this, id](Json result) {
      if (result.value("ok", false)) {
        std::lock_guard<std::mutex> lock(session_mutex_);
        room_joined_ = false;
        EmitResponse(id, "room.leave", true, 0, {}, "{}");
      } else {
        EmitResponse(id, "room.leave", false, ResponseCode(result),
                     ResponseMessage(result));
      }
    });
  });
}

void EngineCore::HandleSessionDisconnect(const Command& command) {
  {
    std::lock_guard<std::mutex> lock(session_mutex_);
    session_ready_ = false;
    room_joined_ = false;
  }
  const uint64_t id = command.id;
  asio::post(signaling_io_, [this, id] {
    heartbeat_timer_.cancel();
    if (signaling_) signaling_->Dispose();
    EmitResponse(id, "session.disconnect", true, 0, {}, "{}");
    EmitNotification("session.closed", "{\"reason\":\"local_disconnect\"}");
  });
}

void EngineCore::ScheduleHeartbeat(int seconds) {
  heartbeat_seconds_ = std::clamp(seconds, 1, 30);
  heartbeat_timer_.expires_after(std::chrono::seconds(heartbeat_seconds_));
  heartbeat_timer_.async_wait([this](const asio::error_code& error) {
    if (error) return;
    bool ready = false;
    {
      std::lock_guard<std::mutex> lock(session_mutex_);
      ready = session_ready_;
    }
    if (!ready || !signaling_) return;
    signaling_->Request("session.ping", Json::object(), [this](Json result) {
      if (result.value("ok", false)) return;
      std::ostringstream data;
      data << "{\"code\":" << ResponseCode(result) << ",\"message\":\""
           << EscapeJson(ResponseMessage(result)) << "\"}";
      EmitNotification("session.error", data.str());
    });
    ScheduleHeartbeat(heartbeat_seconds_);
  });
}
void EngineCore::StopSignaling() {
  if (!signaling_thread_.joinable()) return;
  std::promise<void> completed;
  auto future = completed.get_future();
  asio::post(signaling_io_, [this, &completed] {
    heartbeat_timer_.cancel();
    if (signaling_) signaling_->Dispose();
    completed.set_value();
  });
  future.wait_for(std::chrono::seconds(2));
  signaling_work_.reset();
  signaling_io_.stop();
  signaling_thread_.join();
  signaling_.reset();
}
void EngineCore::OnCameraFrame(const camera_win::I420Frame& frame) {
  if (options_.on_local_frame == nullptr) return;
  rtcwin_frame_view view{};
  view.mid = "local:camera";
  view.i420_y = frame.y.data();
  view.i420_u = frame.u.data();
  view.i420_v = frame.v.data();
  view.width = frame.width;
  view.height = frame.height;
  view.stride_y = frame.stride_y;
  view.stride_u = frame.stride_u;
  view.stride_v = frame.stride_v;
  view.timestamp_us = frame.timestamp_us;
  view.source = 0;
  options_.on_local_frame(options_.userdata, &view);
}

void EngineCore::EmitResponse(uint64_t id, const std::string& method, bool ok,
                              int error_code, const std::string& message,
                              const std::string& data_json) {
  if (options_.on_event == nullptr) return;
  std::ostringstream json;
  json << "{\"v\":1,\"type\":\"res\",\"id\":" << id
       << ",\"method\":\"" << EscapeJson(method) << "\",\"ok\":"
       << (ok ? "true" : "false");
  if (ok) {
    json << ",\"data\":" << (data_json.empty() ? "{}" : data_json);
  } else {
    json << ",\"error\":" << ErrorJson(error_code, message);
  }
  json << "}";
  const std::string text = json.str();
  options_.on_event(options_.userdata, method.c_str(), text.c_str());
}

void EngineCore::EmitNotification(const std::string& method,
                                  const std::string& data_json) {
  if (options_.on_event == nullptr) return;
  std::ostringstream json;
  json << "{\"v\":1,\"type\":\"ntf\",\"method\":\""
       << EscapeJson(method) << "\",\"data\":"
       << (data_json.empty() ? "{}" : data_json) << "}";
  const std::string text = json.str();
  options_.on_event(options_.userdata, method.c_str(), text.c_str());
}

int EngineCore::ParseIntField(const std::string& json, const std::string& name,
                              int fallback) {
  const std::string key = "\"" + name + "\"";
  size_t pos = json.find(key);
  if (pos == std::string::npos) return fallback;
  pos = json.find(':', pos + key.size());
  if (pos == std::string::npos) return fallback;
  ++pos;
  while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) ++pos;
  bool negative = false;
  if (pos < json.size() && json[pos] == '-') {
    negative = true;
    ++pos;
  }
  int value = 0;
  bool found = false;
  while (pos < json.size() && json[pos] >= '0' && json[pos] <= '9') {
    found = true;
    value = value * 10 + (json[pos] - '0');
    ++pos;
  }
  if (!found) return fallback;
  return negative ? -value : value;
}

}  // namespace rtcwin::engine
