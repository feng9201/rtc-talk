#include "rtcwin/engine/engine_core.h"

#include "engine_log.h"
#include "rtcwin/signaling/protocol.h"
#include "rtcwin/webrtc_media/audio_device_service.h"
#include "w_log_cpp.h"

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

std::vector<webrtc_media::AudioIceServer> ParseIceServers(const Json& value) {
  std::vector<webrtc_media::AudioIceServer> result;
  if (!value.is_array()) return result;
  for (const auto& item : value) {
    if (!item.is_object()) continue;
    webrtc_media::AudioIceServer server;
    if (item.contains("urls")) {
      const Json& urls = item["urls"];
      if (urls.is_string()) {
        server.urls.push_back(urls.get<std::string>());
      } else if (urls.is_array()) {
        for (const auto& url : urls) {
          if (url.is_string()) server.urls.push_back(url.get<std::string>());
        }
      }
    }
    if (server.urls.empty() && item.contains("url") &&
        item["url"].is_string()) {
      server.urls.push_back(item["url"].get<std::string>());
    }
    server.username = item.value("username", std::string());
    server.password = item.value("credential",
                                 item.value("password", std::string()));
    if (!server.urls.empty()) result.push_back(std::move(server));
  }
  return result;
}

}  // namespace

EngineCore::EngineCore(const rtcwin_options& options, std::string config_json)
    : options_(options), config_json_(std::move(config_json)) {
  InitializeEngineLogging();
  LOG_INFO("engine starting version={} log={}", "0.1.0", EngineLogPath());
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
    if (stopping_) return;
    stopping_ = true;
  }
  // Stop signaling first so no callback can enqueue work while the worker and
  // media session are being torn down.
  StopSignaling();
  cv_.notify_all();
  if (worker_.joinable()) worker_.join();
  camera_.Stop();
  ShutdownEngineLogging();
}

uint64_t EngineCore::Submit(std::string method, std::string json) {
  constexpr std::size_t kMaxPendingCommands = 256;
  const uint64_t id = next_id_.fetch_add(1);
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (stopping_ || commands_.size() >= kMaxPendingCommands) return 0;
    commands_.push_back(Command{id, std::move(method), std::move(json), {}});
  }
  cv_.notify_one();
  return id;
}

void EngineCore::PostToWorker(std::function<void()> task) {
  if (!task) return;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    if (stopping_ || commands_.size() >= 256) return;
    commands_.push_back(Command{0, {}, {}, std::move(task)});
  }
  cv_.notify_one();
}

void EngineCore::PostToSignaling(std::function<void()> task) {
  if (!task) return;
  asio::post(signaling_io_, std::move(task));
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
    } else {
      HandleCommand(command);
    }
  }
  camera_.Stop();
  ResetAudioMedia();
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
  } else if (command.method == "media.setMute") {
    HandleMediaSetMute(command);
  } else if (command.method == "media.setVolume") {
    HandleMediaSetVolume(command);
  } else if (command.method == "media.setDeafened") {
    HandleMediaSetDeafened(command);
  } else if (command.method == "media.stats") {
    HandleMediaStats(command);
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
    EmitResponse(command.id, command.method, false, 400,
                 "连接参数不是有效 JSON");
    return;
  }

  const std::string url = request.value("url", std::string());
  const std::string token = request.value("token", std::string());
  const std::string uid = request.value("user_id", std::string());
  const std::string room_id = request.value("room_id", std::string());
  const std::string display = request.value("display", uid);
  const std::string media_mode = request.value("media_mode", std::string("auto"));
  const std::string talk_mode = request.value("talk_mode", std::string("free"));
  LOG_INFO("join request uid={} room={} backend={} source={} mode={} talk={}",
           uid, room_id, request.value("audio_backend", std::string()),
           request.value("audio_source", std::string()), media_mode, talk_mode);
  if (url.rfind("ws://", 0) != 0 || token.empty() || uid.empty() ||
      room_id.empty()) {
    EmitResponse(command.id, command.method, false, 400,
                 "信令地址、Token、用户标识和房间 ID 不能为空");
    return;
  }
  if (media_mode != "auto" && media_mode != "sfu" && media_mode != "p2p") {
    EmitResponse(command.id, command.method, false, 400, "媒体模式无效");
    return;
  }
  if (talk_mode != "free" && talk_mode != "ptt" &&
      talk_mode != "moderated") {
    EmitResponse(command.id, command.method, false, 400, "发言模式无效");
    return;
  }

  webrtc_media::AudioMediaConfig media_config;
  media_config.audio_backend =
      request.value("audio_backend", std::string("wasapi"));
  media_config.audio_source =
      request.value("audio_source", std::string("mic"));
  media_config.recording_device =
      request.value("recording_device", std::string());
  media_config.playout_device =
      request.value("playout_device", std::string());
  media_config.volume = std::clamp(request.value("volume", 60), 0, 100);
  media_config.muted = true;
  const bool audio_requested = media_config.audio_backend != "none";

  {
    std::lock_guard<std::mutex> lock(session_mutex_);
    session_url_ = url;
    session_token_ = token;
    session_uid_ = uid;
    session_room_id_ = room_id;
    session_display_ = display;
    media_config_ = std::move(media_config);
    media_mode_ = media_mode == "auto" ? "auto" : media_mode;
    talk_mode_ = talk_mode;
    session_ready_ = false;
    room_joined_ = false;
    media_enabled_ = false;
    deafened_ = false;
  }
  p2p_session_.clear();
  p2p_peer_.clear();
  p2p_offerer_ = false;
  sfu_paused_ = false;
  session_command_id_.store(command.id);
  EmitNotification("session.state", "{\"state\":\"connecting\"}");

  asio::post(signaling_io_, [this, url, token, uid, room_id, display,
                             media_mode, talk_mode, audio_requested] {
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
    handlers.on_open = [this, uid, room_id, display, media_mode, talk_mode,
                        audio_requested] {
      Json hello = {
          {"client",
           {{"platform", "windows-x64"},
            {"version", "0.1.0"},
            {"sdk", "rtcwin-s3"}}},
          {"caps",
           {{"audio", {{"codecs", Json::array({"opus"})}, {"aec", "webrtc"}}},
            {"p2p", true},
            {"p2p_video", false},
            {"sfu_sdp", "unified-plan"}}}};
      signaling_->Request("session.hello", hello, [this, uid, room_id, display,
                                                   media_mode, talk_mode, audio_requested](
                                                      Json result) {
        if (!result.value("ok", false)) {
          const uint64_t id = session_command_id_.exchange(0);
          if (id != 0) {
            EmitResponse(id, "session.connectAndJoin", false,
                         ResponseCode(result), ResponseMessage(result));
          }
          return;
        }
        const Json hello_data = result.value("data", Json::object());
        LOG_INFO("session hello ok session={} ice_servers={} heartbeat={}",
                 hello_data.value("session_id", std::string()),
                 hello_data.value("ice_servers", Json::array()).size(),
                 hello_data.value("heartbeat_interval", 15));
        const auto ice_servers =
            ParseIceServers(hello_data.value("ice_servers", Json::array()));
        {
          std::lock_guard<std::mutex> lock(session_mutex_);
          session_ready_ = true;
          media_config_.ice_servers = ice_servers;
        }
        ScheduleHeartbeat(hello_data.value("heartbeat_interval", 15));
        Json join = {
            {"room_id", room_id},
            {"display", display.empty() ? uid : display},
            {"media",
             {{"audio", {{"send", audio_requested}, {"muted", true}}},
              {"video", {{"send", false}}}}},
            {"policy", {{"talk_mode", talk_mode}, {"media_mode", media_mode}}}};
        signaling_->Request("room.join", join, [this, room_id](Json joined) {
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
          LOG_INFO("room join ok room={} media_mode={} peers={}", room_id,
                   data.value("media_mode", std::string("sfu")),
                   data.value("peers", Json::array()).size());
          const uint64_t id = session_command_id_.exchange(0);
          PostToWorker([this, id, data] { FinalizeJoin(id, data); });
        });
      });
    };

    handlers.on_message = [this](Json message) {
      const std::string type = message.value("type", std::string());
      const std::string method = message.value("method", std::string());
      const Json data = message.value("data", Json::object());
      if (type == "req") {
        const std::string request_id = message.value("id", std::string());
        if (method == "room.mediaMode") {
          HandleMediaModeRequest(request_id, data);
        } else if (method == "sfu.offer") {
          HandleSfuOfferRequest(request_id, data);
        } else {
          signaling_->Send(signaling::Failure(
              request_id, 501, "该客户端尚不支持此请求"));
        }
        return;
      }
      if (method == "p2p.offer") {
        PostToWorker([this, data] {
          ApplyP2pOffer(data.value("session", std::string()),
                        data.value("from", std::string()),
                        data.value("sdp", std::string()));
        });
        return;
      }
      if (method == "p2p.answer") {
        PostToWorker([this, data] {
          ApplyP2pAnswer(data.value("session", std::string()),
                         data.value("sdp", std::string()));
        });
        return;
      }
      if (method == "p2p.candidate") {
        PostToWorker([this, data] {
          ApplyP2pCandidate(data.value("session", std::string()),
                            data.value("candidate", std::string()),
                            data.value("sdp_mid", std::string("0")),
                            data.value("sdp_mline_index", 0));
        });
        return;
      }
      if (method == "p2p.close") {
        PostToWorker([this] {
          if (audio_media_) audio_media_->CloseP2p();
          p2p_session_.clear();
          p2p_peer_.clear();
          p2p_offerer_ = false;
          if (sfu_paused_) {
            sfu_paused_ = false;
            if (audio_media_) {
              webrtc_media::AudioMediaError error;
              audio_media_->SetSfuPaused(false, &error);
            }
          }
        });
        return;
      }
      if (method == "media.forceMute") {
        PostToWorker([this] {
          if (audio_media_) {
            webrtc_media::AudioMediaError error;
            audio_media_->SetMuted(true, &error);
          }
          EmitNotification("media.muted", "{\"muted\":true,\"forced\":true}");
        });
        return;
      }
      if (!method.empty() && message.contains("data")) {
        if (method == "room.peerUpdated" ||
            method == "room.peerJoined") {
          const Json audio = data.value("audio", Json::object());
          LOG_INFO("room peer audio method={} uid={} muted={} speaking={}",
                   method, data.value("uid", std::string()),
                   audio.value("muted", true),
                   audio.value("speaking", false));
        }
        EmitNotification(method, data.dump());
      }
    };

    handlers.on_closed = [this](
                             const signaling::SignalingClient::CloseInfo& info) {
      const uint64_t id = session_command_id_.exchange(0);
      if (id != 0) {
        EmitResponse(id, "session.connectAndJoin", false,
                     info.status != 0 ? info.status : 480, "信令连接关闭");
      }
      {
        std::lock_guard<std::mutex> lock(session_mutex_);
        session_ready_ = false;
        room_joined_ = false;
        media_enabled_ = false;
      }
      heartbeat_timer_.cancel();
      PostToWorker([this] { ResetAudioMedia(); });
      std::ostringstream data;
      data << "{\"handshake_failed\":"
           << (info.handshake_failed ? "true" : "false")
           << ",\"fatal\":" << (info.fatal ? "true" : "false")
           << ",\"status\":" << info.status << ",\"code\":" << info.code
           << "}";
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

void EngineCore::FinalizeJoin(uint64_t command_id, const Json& data) {
  bool audio_requested = true;
  {
    std::lock_guard<std::mutex> lock(session_mutex_);
    audio_requested = media_config_.audio_backend != "none";
  }
  std::string error;
  bool initialized = false;
  if (audio_requested) {
    initialized = InitializeAudioMedia(data, &error);
  }
  {
    std::lock_guard<std::mutex> lock(session_mutex_);
    media_enabled_ = initialized;
  }
  Json response = data;
  response["media_enabled"] = initialized;
  if (command_id != 0) {
    EmitResponse(command_id, "session.connectAndJoin", true, 0, {},
                 response.dump());
  }
  EmitNotification("session.ready", response.dump());
  if (!audio_requested) {
    LOG_INFO("join finalized without audio backend=none");
    EmitNotification("media.disabled", "{\"reason\":\"backend_none\"}");
    return;
  }
  if (!initialized) {
    LOG_ERR("audio media initialization failed error={}", error);
    EmitMediaError(1710, "音频媒体初始化失败", error);
    return;
  }
  LOG_INFO("join finalized media_enabled=true");
  StartMediaForCurrentMode();
}

bool EngineCore::InitializeAudioMedia(const Json& join_data,
                                      std::string* error) {
  ResetAudioMedia();
  webrtc_media::AudioMediaConfig config;
  webrtc_media::AudioMediaStatus status;
  {
    std::lock_guard<std::mutex> lock(session_mutex_);
    config = media_config_;
    if (join_data.contains("media_mode") &&
        join_data["media_mode"].is_string()) {
      media_mode_ = join_data["media_mode"].get<std::string>();
    }
  }
  if (media_mode_ == "p2p" && join_data.contains("p2p") &&
      join_data["p2p"].is_object()) {
    const Json& p2p = join_data["p2p"];
    p2p_session_ = p2p.value("session", std::string());
    p2p_peer_ = p2p.value("peer", std::string());
    p2p_offerer_ = p2p.value("role", std::string()) == "offerer";
  }
  audio_media_ = std::make_unique<webrtc_media::AudioMediaSession>();
  webrtc_media::AudioMediaError media_error;
  const bool ok = audio_media_->Initialize(
      config,
      [this](std::string method, std::string data) {
        PostToWorker([this, method = std::move(method),
                      data = std::move(data)] {
          HandleAudioMediaEvent(method, data);
        });
      },
      &media_error);
  if (!ok) {
    if (error) *error = media_error.message;
    audio_media_.reset();
    return false;
  }
  status = audio_media_->status();
  {
    std::lock_guard<std::mutex> lock(session_mutex_);
    media_enabled_ = true;
  }
  EmitNotification("media.ready",
                   std::string("{\"recording_device\":\"") +
                       EscapeJson(status.recording_device) +
                       "\",\"playout_device\":\"" +
                       EscapeJson(status.playout_device) + "\"}");
  LOG_INFO("audio media ready source={} recording_device={} playout_device={}",
           config.audio_source, status.recording_device, status.playout_device);
  return true;
}

void EngineCore::StartMediaForCurrentMode() {
  std::string mode;
  {
    std::lock_guard<std::mutex> lock(session_mutex_);
    mode = media_mode_;
  }
  if (mode == "p2p") {
    StartP2p();
  } else {
    PublishSfu();
  }
}
void EngineCore::PublishSfu() {
  if (!audio_media_) {
    EmitMediaError(1730, "音频媒体未就绪", "SFU 发布被跳过");
    return;
  }
  webrtc_media::AudioSfuOffer offer;
  webrtc_media::AudioMediaError error;
  if (!audio_media_->CreateSfuPublishOffer(&offer, &error)) {
    LOG_ERR("sfu offer failed code={} error={}", error.code, error.message);
    if (!pending_sfu_request_id_.empty()) {
      FailPendingSfuSwitch(error.code == 0 ? 1730 : error.code,
                           error.message.empty()
                               ? "生成 SFU 音频 offer 失败"
                               : error.message);
    } else {
      EmitMediaError(error.code, "生成 SFU 音频 offer 失败", error.message);
    }
    return;
  }
  Json tracks = Json::array();
  tracks.push_back({{"mid", offer.audio_mid},
                    {"kind", "audio"},
                    {"source", offer.source}});
  Json request = {{"sdp", offer.sdp}, {"tracks", tracks}};
  LOG_INFO("sfu publish offer source={} mid={} sdp_bytes={}", offer.source,
           offer.audio_mid, offer.sdp.size());
  PostToSignaling([this, request = std::move(request)] {
    if (!signaling_) return;
    signaling_->Request(
        "sfu.publish", request, [this](Json result) {
          if (!result.value("ok", false)) {
            const int code = ResponseCode(result);
            const std::string message = ResponseMessage(result);
            LOG_ERR("sfu publish rejected code={} message={}", code, message);
            PostToWorker([this, code, message] {
              if (!pending_sfu_request_id_.empty()) {
                FailPendingSfuSwitch(code, message);
              } else {
                EmitMediaError(code, "SFU 音频发布失败", message);
              }
            });
            return;
          }
          const std::string answer = result.value("data", Json::object())
                                         .value("sdp", std::string());
          PostToWorker([this, answer] { ApplySfuAnswer(answer); });
        });
  });
  EmitNotification("media.publishing",
                   std::string("{\"mode\":\"sfu\",\"sdp_bytes\":") +
                       std::to_string(offer.sdp.size()) + "}");
}

void EngineCore::ApplySfuAnswer(const std::string& sdp) {
  if (!audio_media_) return;
  if (sdp.empty()) {
    if (!pending_sfu_request_id_.empty()) {
      FailPendingSfuSwitch(1731, "SFU publish answer 为空");
    } else {
      EmitMediaError(1731, "应用 SFU answer 失败", "SDP 为空");
    }
    return;
  }
  webrtc_media::AudioMediaError error;
  if (!audio_media_->ApplySfuPublishAnswer(sdp, &error)) {
    if (!pending_sfu_request_id_.empty()) {
      FailPendingSfuSwitch(error.code, error.message);
    } else {
      EmitMediaError(error.code, "应用 SFU answer 失败", error.message);
    }
    return;
  }
  sfu_publish_ready_ = true;
  sfu_paused_ = !p2p_session_.empty();
  LOG_INFO("sfu publish answer applied sdp_bytes={} paused={}", sdp.size(),
           sfu_paused_);
  if (!audio_media_->SetSfuPaused(sfu_paused_, &error)) {
    EmitMediaError(error.code, "更新 SFU 暂停状态失败", error.message);
  }
  EmitNotification("media.published", "{\"ok\":true,\"mode\":\"sfu\"}");
  if (!pending_sfu_request_id_.empty()) TryCompleteSfuSwitch(false);
}

void EngineCore::StartP2p() {
  if (!audio_media_) {
    EmitMediaError(1731, "音频媒体未就绪", "P2P 初始化被跳过");
    return;
  }
  if (p2p_session_.empty() || p2p_peer_.empty()) {
    LOG_WARN("p2p allocation missing, falling back to sfu");
    EmitNotification("p2p.state",
                     "{\"state\":\"failed\",\"reason\":\"missing_allocation\"}");
    EmitMediaError(1732, "P2P 分配参数缺失", "回退到 SFU 音频");
    {
      std::lock_guard<std::mutex> lock(session_mutex_);
      media_mode_ = "sfu";
    }
    PublishSfu();
    return;
  }
  if (!p2p_offerer_) {
    LOG_INFO("p2p awaiting offer session={} peer={}", p2p_session_, p2p_peer_);
    EmitNotification("p2p.state",
                     "{\"session\":\"" + EscapeJson(p2p_session_) +
                         "\",\"state\":\"awaiting_offer\"}");
    return;
  }
  std::string offer;
  webrtc_media::AudioMediaError error;
  if (!audio_media_->CreateP2pOffer(&offer, &error)) {
    LOG_ERR("p2p offer failed code={} error={}", error.code, error.message);
    EmitMediaError(error.code, "创建 P2P 音频 offer 失败", error.message);
    EmitNotification("p2p.state",
                     "{\"session\":\"" + EscapeJson(p2p_session_) +
                         "\",\"state\":\"failed\"}");
    return;
  }
  Json event = {{"session", p2p_session_}, {"to", p2p_peer_}, {"sdp", offer}};
  LOG_INFO("p2p offer sent session={} peer={} sdp_bytes={}", p2p_session_,
           p2p_peer_, offer.size());
  PostToSignaling([this, event = std::move(event)] {
    if (signaling_) signaling_->Notify("p2p.offer", event);
  });
  EmitNotification("p2p.started",
                   std::string("{\"session\":\"") +
                       EscapeJson(p2p_session_) + "\",\"peer\":\"" +
                       EscapeJson(p2p_peer_) + "\",\"role\":\"offerer\"}");
}

void EngineCore::ApplyP2pOffer(const std::string& session,
                               const std::string& from,
                               const std::string& sdp) {
  if (session.empty() || from.empty() || sdp.empty()) return;
  LOG_INFO("p2p offer received session={} from={} sdp_bytes={}", session, from,
           sdp.size());
  if (p2p_session_.empty()) {
    p2p_session_ = session;
    p2p_peer_ = from;
    p2p_offerer_ = false;
    std::lock_guard<std::mutex> lock(session_mutex_);
    media_mode_ = "p2p";
  }
  if (session != p2p_session_ || !audio_media_) return;
  p2p_peer_ = from;
  p2p_offerer_ = false;
  std::string answer;
  webrtc_media::AudioMediaError error;
  if (!audio_media_->CreateP2pAnswer(sdp, &answer, &error)) {
    LOG_ERR("p2p answer create failed session={} code={} error={}", session,
              error.code, error.message);
    EmitMediaError(error.code, "创建 P2P 音频 answer 失败", error.message);
    EmitNotification("p2p.state",
                     "{\"session\":\"" + EscapeJson(session) +
                         "\",\"state\":\"failed\"}");
    return;
  }
  Json event = {{"session", session}, {"to", from}, {"sdp", answer}};
  PostToSignaling([this, event = std::move(event)] {
    if (signaling_) signaling_->Notify("p2p.answer", event);
  });
  EmitNotification("p2p.started",
                   std::string("{\"session\":\"") + EscapeJson(session) +
                       "\",\"peer\":\"" + EscapeJson(from) +
                       "\",\"role\":\"answerer\"}");
}

void EngineCore::ApplyP2pAnswer(const std::string& session,
                                const std::string& sdp) {
  if (session != p2p_session_ || !p2p_offerer_ || sdp.empty() ||
      !audio_media_) {
    return;
  }
  webrtc_media::AudioMediaError error;
  if (!audio_media_->ApplyP2pAnswer(sdp, &error)) {
    LOG_ERR("p2p answer apply failed session={} code={} error={}", session,
              error.code, error.message);
    EmitMediaError(error.code, "应用 P2P 音频 answer 失败", error.message);
    EmitNotification("p2p.state",
                     "{\"session\":\"" + EscapeJson(session) +
                         "\",\"state\":\"failed\"}");
  }
}

void EngineCore::ApplyP2pCandidate(const std::string& session,
                                   const std::string& candidate,
                                   const std::string& mid, int index) {
  if (session != p2p_session_ || !audio_media_ || candidate.empty()) return;
  webrtc_media::AudioMediaError error;
  if (!audio_media_->AddP2pCandidate(candidate, mid, index, &error)) {
    EmitMediaError(error.code, "添加 P2P ICE 候选失败", error.message);
  }
}

void EngineCore::HandleMediaModeRequest(const std::string& request_id,
                                        const Json& data) {
  PostToWorker([this, request_id, data] {
    const std::string mode =
        data.value("mode", data.value("media_mode", std::string()));
    LOG_INFO("media mode request id={} mode={}", request_id, mode);
    if (!audio_media_) {
      PostToSignaling([this, request_id] {
        if (signaling_) {
          signaling_->Send(signaling::Failure(request_id, 409,
                                              "音频媒体未就绪"));
        }
      });
      return;
    }
    if (mode == "p2p") {
      const Json allocation = data.value("p2p", Json::object());
      if (!allocation.is_object() || allocation.empty()) {
        PostToSignaling([this, request_id] {
          if (signaling_) {
            signaling_->Send(signaling::Failure(request_id, 400,
                                                "缺少 P2P 分配"));
          }
        });
        return;
      }
      pending_sfu_request_id_.clear();
      pending_sfu_room_mode_json_.clear();
      sfu_publish_ready_ = false;
      sfu_subscribe_ready_ = false;
      PostToSignaling([this] { media_mode_timer_.cancel(); });
      p2p_session_ = allocation.value("session", std::string());
      p2p_peer_ = allocation.value("peer", std::string());
      p2p_offerer_ = allocation.value("role", std::string()) == "offerer";
      {
        std::lock_guard<std::mutex> lock(session_mutex_);
        media_mode_ = "p2p";
      }
      StartP2p();
      EmitNotification("room.mediaMode", data.dump());
      PostToSignaling([this, request_id] {
        if (signaling_) {
          signaling_->Send(signaling::Response(request_id, Json::object()));
        }
      });
      return;
    }
    if (mode == "sfu") {
      pending_sfu_request_id_ = request_id;
      pending_sfu_room_mode_json_ = data.dump();
      sfu_publish_ready_ = false;
      sfu_subscribe_ready_ = false;
      // Keep P2P live until publish and subscribe are both ready. The SFU
      // sender remains paused while the existing P2P link is active.
      PublishSfu();
      if (pending_sfu_request_id_ == request_id) {
        ScheduleSfuSwitchTimeout(request_id);
      }
      return;
    }
    PostToSignaling([this, request_id] {
      if (signaling_) {
        signaling_->Send(signaling::Failure(request_id, 400, "媒体模式无效"));
      }
    });
  });
}

void EngineCore::HandleSfuOfferRequest(const std::string& request_id,
                                       const Json& data) {
  const std::string sdp = data.value("sdp", std::string());
  if (sdp.empty()) {
    PostToSignaling([this, request_id] {
      if (signaling_) {
        signaling_->Send(signaling::Failure(request_id, 400, "缺少 SDP"));
      }
    });
    return;
  }
  PostToWorker([this, request_id, sdp] {
    if (!audio_media_) {
      PostToSignaling([this, request_id] {
        if (signaling_) {
          signaling_->Send(signaling::Failure(request_id, 409,
                                              "音频媒体未就绪"));
        }
      });
      return;
    }
    std::string answer;
    webrtc_media::AudioMediaError error;
    const bool ok = audio_media_->CreateSfuSubscribeAnswer(sdp, &answer, &error);
    PostToSignaling([this, request_id, ok, answer, error] {
      if (!signaling_) return;
      if (ok) {
        signaling_->Send(
            signaling::Response(request_id, Json{{"sdp", answer}}));
        PostToWorker([this] {
          sfu_subscribe_ready_ = true;
          if (!pending_sfu_request_id_.empty()) TryCompleteSfuSwitch(false);
        });
      } else {
        signaling_->Send(signaling::Failure(
            request_id, error.code == 0 ? 500 : error.code, error.message));
        PostToWorker([this, error] {
          if (!pending_sfu_request_id_.empty()) {
            FailPendingSfuSwitch(error.code == 0 ? 500 : error.code,
                                 error.message.empty()
                                     ? "SFU subscribe 失败"
                                     : error.message);
          }
        });
      }
    });
  });
}

void EngineCore::TryCompleteSfuSwitch(bool timed_out) {
  if (pending_sfu_request_id_.empty()) return;
  if (timed_out) {
    FailPendingSfuSwitch(504, "SFU 切换超时，保留当前 P2P 音频");
    return;
  }
  if (!sfu_publish_ready_ || !sfu_subscribe_ready_) return;

  const std::string request_id = pending_sfu_request_id_;
  const std::string room_mode_json = pending_sfu_room_mode_json_;
  pending_sfu_request_id_.clear();
  pending_sfu_room_mode_json_.clear();
  sfu_publish_ready_ = false;
  sfu_subscribe_ready_ = false;
  PostToSignaling([this] { media_mode_timer_.cancel(); });

  if (audio_media_) audio_media_->CloseP2p();
  p2p_session_.clear();
  p2p_peer_.clear();
  p2p_offerer_ = false;
  sfu_paused_ = false;
  webrtc_media::AudioMediaError error;
  if (audio_media_ && !audio_media_->SetSfuPaused(false, &error)) {
    EmitMediaError(error.code, "恢复 SFU 音频失败", error.message);
  }
  {
    std::lock_guard<std::mutex> lock(session_mutex_);
    media_mode_ = "sfu";
  }
  EmitNotification("room.mediaMode",
                   room_mode_json.empty() ? "{\"mode\":\"sfu\"}"
                                          : room_mode_json);
  PostToSignaling([this, request_id] {
    if (signaling_) {
      signaling_->Send(signaling::Response(request_id, Json::object()));
    }
  });
}

void EngineCore::FailPendingSfuSwitch(int code, const std::string& message) {
  if (pending_sfu_request_id_.empty()) return;
  const std::string request_id = pending_sfu_request_id_;
  pending_sfu_request_id_.clear();
  pending_sfu_room_mode_json_.clear();
  sfu_publish_ready_ = false;
  sfu_subscribe_ready_ = false;
  PostToSignaling([this] { media_mode_timer_.cancel(); });

  if (!p2p_session_.empty()) {
    sfu_paused_ = true;
    if (audio_media_) {
      webrtc_media::AudioMediaError ignored;
      audio_media_->SetSfuPaused(true, &ignored);
    }
  }
  EmitMediaError(code == 0 ? 1732 : code, "SFU 媒体切换失败", message);
  PostToSignaling([this, request_id, code, message] {
    if (signaling_) {
      signaling_->Send(signaling::Failure(
          request_id, code == 0 ? 500 : code,
          message.empty() ? "SFU 媒体切换失败" : message));
    }
  });
}

void EngineCore::ScheduleSfuSwitchTimeout(const std::string& request_id) {
  PostToSignaling([this, request_id] {
    if (!signaling_) return;
    media_mode_timer_.expires_after(std::chrono::seconds(5));
    media_mode_timer_.async_wait([this, request_id](
                                     const asio::error_code& error) {
      if (error) return;
      PostToWorker([this, request_id] {
        if (pending_sfu_request_id_ == request_id) {
          TryCompleteSfuSwitch(true);
        }
      });
    });
  });
}
void EngineCore::HandleAudioMediaEvent(const std::string& method,
                                       const std::string& data_json) {
  Json data;
  try {
    data = Json::parse(data_json);
  } catch (const std::exception&) {
    return;
  }
  if (method == "p2p.candidate") {
    if (p2p_session_.empty()) return;
    Json candidate = data;
    candidate.erase("pc");
    candidate["session"] = p2p_session_;
    candidate["to"] = p2p_peer_;
    PostToSignaling([this, candidate = std::move(candidate)] {
      if (signaling_) signaling_->Notify("p2p.candidate", candidate);
    });
    return;
  }
  if (method == "audio.connection") {
    const std::string pc = data.value("pc", std::string());
    const std::string state = data.value("state", std::string());
    LOG_INFO("audio connection pc={} state={}", pc, state);
    if (pc == "p2p") {
      if (state == "connected") {
        sfu_paused_ = true;
        if (audio_media_) {
          webrtc_media::AudioMediaError error;
          audio_media_->SetSfuPaused(true, &error);
        }
      } else if (state == "failed" || state == "closed") {
        if (sfu_paused_) {
          sfu_paused_ = false;
          if (audio_media_) {
            webrtc_media::AudioMediaError error;
            audio_media_->SetSfuPaused(false, &error);
          }
        }
      }
      Json out = {{"session", p2p_session_}, {"state", state}};
      PostToSignaling([this, out = std::move(out)] {
        if (signaling_) signaling_->Notify("p2p.state", out);
      });
    }
    EmitNotification("media.state", data.dump());
    return;
  }
  if (method == "audio.track") {
    EmitNotification("media.track", data.dump());
  }
}

void EngineCore::ResetAudioMedia() {
  if (audio_media_) {
    audio_media_->Shutdown();
    audio_media_.reset();
  }
  p2p_session_.clear();
  p2p_peer_.clear();
  p2p_offerer_ = false;
  sfu_paused_ = false;
  std::lock_guard<std::mutex> lock(session_mutex_);
  media_enabled_ = false;
}

void EngineCore::EmitMediaError(int code, const std::string& message,
                                const std::string& detail) {
  EmitNotification(
      "media.error",
      std::string("{\"code\":") + std::to_string(code) + ",\"message\":\"" +
          EscapeJson(message) + "\",\"detail\":\"" + EscapeJson(detail) +
          "\"}");
}

void EngineCore::HandleMediaSetMute(const Command& command) {
  Json request;
  try {
    request = Json::parse(command.json);
  } catch (const std::exception&) {
    EmitResponse(command.id, command.method, false, 400, "静音参数无效");
    return;
  }
  if (!request.contains("muted") || !request["muted"].is_boolean()) {
    EmitResponse(command.id, command.method, false, 400, "缺少 muted 参数");
    return;
  }
  bool joined = false;
  {
    std::lock_guard<std::mutex> lock(session_mutex_);
    joined = room_joined_ && media_enabled_;
  }
  if (!joined || !audio_media_) {
    EmitResponse(command.id, command.method, false, 409, "音频媒体未就绪");
    return;
  }
  const bool muted = request["muted"].get<bool>();
  LOG_INFO("media.setMute requested muted={}", muted);
  const uint64_t generation = mute_generation_.fetch_add(1) + 1;
  webrtc_media::AudioMediaError media_error;
  if (muted && !audio_media_->SetMuted(true, &media_error)) {
    EmitResponse(command.id, command.method, false, media_error.code,
                 media_error.message);
    return;
  }
  const uint64_t command_id = command.id;
  PostToSignaling([this, command_id, generation, muted] {
    if (!signaling_) {
      PostToWorker([this, command_id, generation, muted] {
        if (generation == mute_generation_.load()) {
          EmitResponse(command_id, "media.setMute", false, 480,
                       "信令连接不可用");
        }
      });
      return;
    }
    signaling_->Request(
        "media.setMute", {{"source", "mic"}, {"muted", muted}},
        [this, command_id, generation, muted](Json result) {
          PostToWorker([this, command_id, generation, muted, result] {
            if (generation != mute_generation_.load()) {
              EmitResponse(command_id, "media.setMute", false, 487,
                           "静音命令已被更新");
              return;
            }
            const bool server_ok = result.value("ok", false);
            const bool effective = muted || !server_ok;
            webrtc_media::AudioMediaError media_error;
            const bool applied =
                audio_media_ &&
                audio_media_->SetMuted(effective, &media_error);
            Json state = {{"muted", applied ? effective : true}};
            LOG_INFO(
                "media.setMute confirmed requested={} server_ok={} "
                "effective={} applied={} error={}",
                muted, server_ok, effective, applied, media_error.message);
            EmitNotification("media.muted", state.dump());
            if (!server_ok) {
              EmitResponse(command_id, "media.setMute", false,
                           ResponseCode(result), ResponseMessage(result));
            } else if (!applied) {
              EmitResponse(command_id, "media.setMute", false,
                           media_error.code, media_error.message);
            } else {
              EmitResponse(command_id, "media.setMute", true, 0, {},
                           state.dump());
            }
          });
        });
  });
}

void EngineCore::HandleMediaSetVolume(const Command& command) {
  Json request;
  try {
    request = Json::parse(command.json);
  } catch (const std::exception&) {
    EmitResponse(command.id, command.method, false, 400, "音量参数无效");
    return;
  }
  double gain = request.value("gain", -1.0);
  if (gain < 0.0 && request.contains("volume")) {
    gain = request.value("volume", 60) / 100.0;
  }
  if (!audio_media_ ||
      !audio_media_->SetVolume(gain, nullptr)) {
    EmitResponse(command.id, command.method, false, 400, "音量必须在 0 到 100 之间");
    return;
  }
  Json data = {{"volume", static_cast<int>(gain * 100.0 + 0.5)}};
  EmitResponse(command.id, command.method, true, 0, {}, data.dump());
}

void EngineCore::HandleMediaSetDeafened(const Command& command) {
  Json request;
  try {
    request = Json::parse(command.json);
  } catch (const std::exception&) {
    EmitResponse(command.id, command.method, false, 400, "收听参数无效");
    return;
  }
  const bool deafened = request.value("deafened", false);
  webrtc_media::AudioMediaError error;
  if (!audio_media_ || !audio_media_->SetDeafened(deafened, &error)) {
    EmitResponse(command.id, command.method, false,
                 error.code == 0 ? 409 : error.code,
                 error.message.empty() ? "音频媒体未就绪" : error.message);
    return;
  }
  deafened_ = deafened;
  Json data = {{"deafened", deafened}};
  EmitNotification("media.deafened", data.dump());
  EmitResponse(command.id, command.method, true, 0, {}, data.dump());
}

void EngineCore::HandleMediaStats(const Command& command) {
  if (!audio_media_) {
    EmitResponse(command.id, command.method, false, 409, "音频媒体未就绪");
    return;
  }
  const auto status = audio_media_->status();
  LOG_INFO(
      "media stats source={} muted={} recording={} capture_frames={} "
      "sfu={}/{} p2p={}/{} sent={} recv={} remote_tracks={} level={}",
      media_config_.audio_source, status.muted, status.recording,
      status.capture_frames, status.sfu_send_state, status.sfu_recv_state,
      status.p2p_state, status.p2p_connected, status.p2p_audio_packets_sent,
      status.audio_packets_received, status.remote_tracks,
      status.remote_audio_level);
  Json data = {
      {"initialized", status.initialized},
      {"sfu_send_ready", status.sfu_send_ready},
      {"sfu_recv_ready", status.sfu_recv_ready},
      {"p2p_ready", status.p2p_ready},
      {"p2p_connected", status.p2p_connected},
      {"muted", status.muted},
      {"deafened", status.deafened},
      {"sfu_paused", status.sfu_paused},
      {"recording_initialized", status.recording_initialized},
      {"recording", status.recording},
      {"playout_initialized", status.playout_initialized},
      {"playing", status.playing},
      {"sfu_send_active", status.sfu_send_active},
      {"p2p_send_active", status.p2p_send_active},
      {"volume", status.volume},
      {"capture_frames", status.capture_frames},
      {"remote_tracks", status.remote_tracks},
      {"active_remote_tracks", status.active_remote_tracks},
      {"sfu_audio_packets_sent", status.sfu_audio_packets_sent},
      {"p2p_audio_packets_sent", status.p2p_audio_packets_sent},
      {"audio_packets_received", status.audio_packets_received},
      {"audio_bytes_sent", status.audio_bytes_sent},
      {"audio_bytes_received", status.audio_bytes_received},
      {"remote_audio_level", status.remote_audio_level},
      {"recording_device", status.recording_device},
      {"playout_device", status.playout_device},
      {"sfu_send_state", status.sfu_send_state},
      {"sfu_recv_state", status.sfu_recv_state},
      {"p2p_state", status.p2p_state},
      {"last_error", status.last_error}};
  EmitResponse(command.id, command.method, true, 0, {}, data.dump());
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
    media_mode_timer_.cancel();
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
  auto completed = std::make_shared<std::promise<void>>();
  auto future = completed->get_future();
  asio::post(signaling_io_, [this, completed] {
    heartbeat_timer_.cancel();
    media_mode_timer_.cancel();
    if (signaling_) signaling_->Dispose();
    completed->set_value();
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
  LOG_INFO("response id={} method={} ok={} code={} message={}", id, method, ok,
           error_code, message);
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
  LOG_INFO("notification method={}", method);
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
