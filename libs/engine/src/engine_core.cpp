#include "rtcwin/engine/engine_core.h"

#include <sstream>
#include <utility>

namespace rtcwin::engine {
namespace {

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

std::string ErrorJson(int code, const std::string& message) {
  return std::string("{\"code\":") + std::to_string(code) +
         ",\"msg\":\"" + EscapeJson(message) + "\"}";
}

}  // namespace

EngineCore::EngineCore(const rtcwin_options& options, std::string config_json)
    : options_(options), config_json_(std::move(config_json)) {
  worker_ = std::thread(&EngineCore::WorkerLoop, this);
}

EngineCore::~EngineCore() {
  {
    std::lock_guard<std::mutex> lock(mutex_);
    stopping_ = true;
  }
  cv_.notify_all();
  if (worker_.joinable()) worker_.join();
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
