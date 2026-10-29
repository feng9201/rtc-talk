#include "rtcwin/engine/engine_core.h"

#include "engine_log.h"
#include "rtcwin/signaling/protocol.h"
#include "rtcwin/webrtc_media/audio_device_service.h"

#include <algorithm>
#include <chrono>
#include <utility>

namespace rtcwin::engine {
namespace {

using signaling::Json;

int ResponseCode(const Json& result) {
  return result.value("error", Json::object()).value("code", 500);
}

std::string ResponseMessage(const Json& result) {
  return result.value("error", Json::object())
      .value("msg", std::string("信令请求失败"));
}

std::vector<webrtc_media::IceServer> ParseIceServers(const Json& value) {
  std::vector<webrtc_media::IceServer> result;
  if (!value.is_array()) return result;
  for (const auto& item : value) {
    if (!item.is_object()) continue;
    webrtc_media::IceServer server;
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

std::vector<webrtc_media::RemoteTrackInfo> ParseRemoteTracks(
    const Json& consumers) {
  std::vector<webrtc_media::RemoteTrackInfo> result;
  if (!consumers.is_array()) return result;
  for (const auto& item : consumers) {
    if (!item.is_object()) continue;
    webrtc_media::RemoteTrackInfo info;
    info.mid = item.value("mid", std::string());
    info.uid = item.value("uid", std::string());
    info.kind = item.value("kind", std::string("audio"));
    info.source = item.value("source", std::string());
    if (info.source.empty()) {
      info.source = info.kind == "video" ? "camera" : "mic";
    }
    if (!info.mid.empty()) result.push_back(std::move(info));
  }
  return result;
}

}  // namespace

EngineCore::EngineCore(const rtcwin_options& options, std::string config_json)
    : options_(options),
      events_(options_),
      config_json_(std::move(config_json)) {
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
  // Invalidate every asynchronous session callback before tearing down the
  // signaling and media owners.
  session_state_.MarkDisconnected();
  // Stop signaling first so no callback can enqueue work while the worker and
  // media session are being torn down.
  signaling_transport_.Stop([this] {
    heartbeat_timer_.cancel();
    media_mode_timer_.cancel();
  });
  cv_.notify_all();
  if (worker_.joinable()) worker_.join();
  camera_.Stop();
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
      command.data = Json::parse(json);
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
    } else {
      if (!command.parse_error.empty()) {
        EmitResponse(command.id, command.method, false, 400,
                     "命令参数不是有效 JSON");
      } else if (!command_router_.Dispatch(
                     EngineCommand{command.id, command.method, command.data})) {
        EmitResponse(command.id, command.method, false, 501, "功能尚未实现");
      }
    }
  }
  camera_.Stop();
  ResetMedia();
}

void EngineCore::RegisterCommandHandlers() {
  command_router_.Register("engine.ping", [this](const EngineCommand& command) {
    EmitResponse(command.id, command.method, true, 0, {}, Json::object());
  });
  command_router_.Register(
      "camera.devices", [this](const EngineCommand& command) {
        HandleCameraDevices(command);
      });
  command_router_.Register(
      "camera.preview.start", [this](const EngineCommand& command) {
        HandleCameraPreviewStart(command);
      });
  command_router_.Register(
      "camera.preview.stop", [this](const EngineCommand& command) {
        HandleCameraPreviewStop(command);
      });
  command_router_.Register(
      "audio.devices", [this](const EngineCommand& command) {
        HandleAudioDevices(command);
      });
  command_router_.Register(
      "session.connectAndJoin", [this](const EngineCommand& command) {
        HandleSessionConnectAndJoin(command);
      });
  command_router_.Register("media.setMute",
                           [this](const EngineCommand& command) {
                             HandleMediaSetMute(command);
                           });
  command_router_.Register("media.toggleCamera",
                           [this](const EngineCommand& command) {
                             HandleMediaToggleCamera(command);
                           });
  command_router_.Register(
      "media.toggleScreenShare", [this](const EngineCommand& command) {
        HandleMediaToggleScreenShare(command);
      });
  command_router_.Register("media.setVolume",
                           [this](const EngineCommand& command) {
                             HandleMediaSetVolume(command);
                           });
  command_router_.Register("media.setDeafened",
                           [this](const EngineCommand& command) {
                             HandleMediaSetDeafened(command);
                           });
  command_router_.Register("media.stats",
                           [this](const EngineCommand& command) {
                             HandleMediaStats(command);
                           });
  command_router_.Register("room.leave",
                           [this](const EngineCommand& command) {
                             HandleRoomLeave(command);
                           });
  command_router_.Register("session.disconnect",
                           [this](const EngineCommand& command) {
                             HandleSessionDisconnect(command);
                           });
}

void EngineCore::HandleCameraDevices(const EngineCommand& command) {
  const auto devices = camera_win::CameraService::Enumerate();
  Json cameras = Json::array();
  for (const auto& device : devices) {
    cameras.push_back(
        {{"id", std::to_string(device.index)}, {"name", device.name}});
  }
  EmitResponse(command.id, command.method, true, 0, {},
               Json{{"cameras", std::move(cameras)}});
}

void EngineCore::HandleCameraPreviewStart(const EngineCommand& command) {
  camera_.Stop();
  camera_preview_active_ = false;
  camera_device_index_ = command.data.value("device_index", -1);
  std::string error;
  if (!EnsureCameraCapture(&error)) {
    EmitResponse(command.id, command.method, false, 1501,
                 error.empty() ? "camera start failed" : error);
    return;
  }
  camera_preview_active_ = true;
  Json data = {{"device_index", camera_device_index_},
               {"device", camera_.device_name()},
               {"width", camera_.width()},
               {"height", camera_.height()},
               {"fps", camera_.fps()}};
  EmitResponse(command.id, command.method, true, 0, {}, data);
}

void EngineCore::HandleCameraPreviewStop(const EngineCommand& command) {
  camera_preview_active_ = false;
  StopCameraCaptureIfIdle();
  EmitResponse(command.id, command.method, true, 0, {}, Json::object());
}

void EngineCore::HandleAudioDevices(const EngineCommand& command) {
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
  EmitResponse(command.id, command.method, true, 0, {}, data);
}
void EngineCore::HandleSessionConnectAndJoin(const EngineCommand& command) {
  if (!command.data.is_object()) {
    EmitResponse(command.id, command.method, false, 400,
                 "连接参数不是有效 JSON");
    return;
  }
  const Json& request = command.data;

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

  webrtc_media::MediaConfig media_config;
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
    talk_mode_ = talk_mode;
    deafened_ = false;
  }
  const uint64_t generation = session_state_.BeginConnect(command.id);
  ResetMedia();
  p2p_session_.clear();
  p2p_peer_.clear();
  p2p_offerer_ = false;
  sfu_paused_ = false;
  EmitNotification("session.state", Json{{"state", "connecting"}});

  const auto fail_session = [this, generation](int code,
                                               const std::string& message) {
    if (!session_state_.IsCurrent(generation)) return;
    const uint64_t id = session_state_.TakeJoinCommand(generation);
    session_state_.MarkFailed(generation);
    if (id != 0) {
      EmitResponse(id, "session.connectAndJoin", false, code, message);
    }
    PostToSignaling([this] {
      heartbeat_timer_.cancel();
      media_mode_timer_.cancel();
    });
    // Media owns worker-thread resources. The generation check prevents a
    // delayed failure handler from tearing down a newer session.
    PostToWorker([this, generation] {
      if (session_state_.IsGeneration(generation)) ResetMedia();
    });
  };

  signaling::SignalingClient::Handlers handlers;
  handlers.on_open = [this, generation, uid, room_id, display, media_mode,
                      talk_mode, audio_requested, fail_session] {
      if (!session_state_.IsCurrent(generation)) return;
      Json hello = {
          {"client",
           {{"platform", "windows-x64"},
            {"version", "0.1.0"},
            {"sdk", "rtcwin-s3"}}},
          {"caps",
           {{"audio", {{"codecs", Json::array({"opus"})}, {"aec", "webrtc"}}},
            {"p2p", true},
            {"p2p_video", false},
            {"video",
             {{"codecs",
               Json::array({{{"name", "h264"}, {"hw", false}}})}}},
            {"sfu_sdp", "unified-plan"}}}};
      signaling_transport_.Request(
          "session.hello", hello,
          [this, generation, uid, room_id, display, media_mode, talk_mode,
           audio_requested, fail_session](Json result) {
        if (!session_state_.IsCurrent(generation)) return;
        if (!result.value("ok", false)) {
          fail_session(ResponseCode(result), ResponseMessage(result));
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
          media_config_.ice_servers = ice_servers;
        }
        if (!session_state_.MarkSignalingReady(generation)) return;
        ScheduleHeartbeat(hello_data.value("heartbeat_interval", 15),
                          generation);
        if (!session_state_.MarkJoiningRoom(generation)) return;
        Json join = {
            {"room_id", room_id},
            {"display", display.empty() ? uid : display},
            {"media",
             {{"audio", {{"send", audio_requested}, {"muted", true}}},
              {"video", {{"send", false}}}}},
            {"policy", {{"talk_mode", talk_mode}, {"media_mode", media_mode}}}};
        signaling_transport_.Request("room.join", join,
                                     [this, generation, room_id,
                                      media_mode, fail_session](Json joined) {
          if (!session_state_.IsCurrent(generation)) return;
          if (!joined.value("ok", false)) {
            fail_session(ResponseCode(joined), ResponseMessage(joined));
            return;
          }
          const Json data = joined.value("data", Json::object());
          LOG_INFO("room join ok room={} media_mode={} peers={}", room_id,
                   data.value("media_mode", std::string("sfu")),
                   data.value("peers", Json::array()).size());
          SessionMediaMode selected_mode =
              SessionStateMachine::ParseMediaMode(
                  data.value("media_mode", std::string()));
          if (selected_mode == SessionMediaMode::Unknown) {
            if (data.contains("p2p") && data["p2p"].is_object()) {
              selected_mode = SessionMediaMode::P2p;
            } else if (media_mode == "p2p") {
              selected_mode = SessionMediaMode::P2p;
            } else {
              selected_mode = SessionMediaMode::Sfu;
            }
          }
          if (!session_state_.MarkJoined(generation, selected_mode, false)) {
            return;
          }
          const uint64_t id = session_state_.TakeJoinCommand(generation);
          PostToWorker(
              [this, id, data, generation] { FinalizeJoin(id, data, generation); });
                                     });
          });
  };

  handlers.on_message = [this, generation](Json message) {
      if (!session_state_.IsCurrent(generation)) return;
      const std::string type = message.value("type", std::string());
      const std::string method = message.value("method", std::string());
      const Json data = message.value("data", Json::object());
      if (type == "req") {
        const std::string request_id = message.value("id", std::string());
        if (method == "room.mediaMode") {
          HandleMediaModeRequest(request_id, data, generation);
        } else if (method == "sfu.offer") {
          HandleSfuOfferRequest(request_id, data, generation);
        } else {
          signaling_transport_.Send(signaling::Failure(
              request_id, 501, "该客户端尚不支持此请求"));
        }
        return;
      }
      if (method == "p2p.offer") {
        PostToWorker([this, data, generation] {
          if (!session_state_.IsCurrent(generation)) return;
          ApplyP2pOffer(data.value("session", std::string()),
                        data.value("from", std::string()),
                        data.value("sdp", std::string()), generation);
        });
        return;
      }
      if (method == "p2p.answer") {
        PostToWorker([this, data, generation] {
          if (!session_state_.IsCurrent(generation)) return;
          ApplyP2pAnswer(data.value("session", std::string()),
                         data.value("sdp", std::string()), generation);
        });
        return;
      }
      if (method == "p2p.candidate") {
        PostToWorker([this, data, generation] {
          if (!session_state_.IsCurrent(generation)) return;
          ApplyP2pCandidate(data.value("session", std::string()),
                            data.value("candidate", std::string()),
                            data.value("sdp_mid", std::string("0")),
                            data.value("sdp_mline_index", 0), generation);
        });
        return;
      }
      if (method == "p2p.close") {
        PostToWorker([this, generation] {
          if (!session_state_.IsCurrent(generation)) return;
          if (media_session_) media_session_->CloseP2p();
          p2p_session_.clear();
          p2p_peer_.clear();
          p2p_offerer_ = false;
          if (sfu_paused_) {
            sfu_paused_ = false;
            if (media_session_) {
              webrtc_media::MediaError error;
              media_session_->SetSfuPaused(false, &error);
            }
          }
        });
        return;
      }
      if (method == "media.forceMute") {
        PostToWorker([this, generation] {
          if (!session_state_.IsCurrent(generation)) return;
          if (media_session_) {
            webrtc_media::MediaError error;
            media_session_->SetMuted(true, &error);
          }
          EmitNotification("media.muted",
                           Json{{"muted", true}, {"forced", true}});
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
        EmitNotification(method, data);
      }
  };

  handlers.on_closed = [this, generation](
                           const signaling::SignalingClient::CloseInfo& info) {
      if (!session_state_.IsCurrent(generation)) return;
      const uint64_t id = session_state_.TakeJoinCommand(generation);
      session_state_.MarkFailed(generation);
      if (id != 0) {
        EmitResponse(id, "session.connectAndJoin", false,
                     info.status != 0 ? info.status : 480, "信令连接关闭");
      }
      heartbeat_timer_.cancel();
      media_mode_timer_.cancel();
      PostToWorker([this, generation] {
        if (session_state_.IsGeneration(generation)) ResetMedia();
      });
      EmitNotification("session.closed",
                       Json{{"handshake_failed", info.handshake_failed},
                            {"fatal", info.fatal},
                            {"status", info.status},
                            {"code", info.code}});
  };

  handlers.on_error = [this, generation](int code, std::string message) {
    if (!session_state_.IsCurrent(generation)) return;
    EmitNotification("session.error",
                     Json{{"code", code}, {"message", std::move(message)}});
  };

  signaling_transport_.Connect(url, token, std::move(handlers),
                               [fail_session](int code, std::string message) {
                                 fail_session(code, std::move(message));
                               });
}

void EngineCore::FinalizeJoin(uint64_t command_id, const Json& data,
                              uint64_t generation) {
  if (!session_state_.IsCurrent(generation) ||
      session_state_.snapshot().phase != SessionPhase::Joined) {
    return;
  }
  bool audio_requested = true;
  {
    std::lock_guard<std::mutex> lock(session_mutex_);
    audio_requested = media_config_.audio_backend != "none";
  }
  std::string error;
  bool initialized = false;
  if (audio_requested) {
    initialized = InitializeMedia(data, &error, generation);
  }
  if (!session_state_.SetMediaEnabled(generation, initialized)) return;
  Json response = data;
  response["media_enabled"] = initialized;
  if (command_id != 0) {
    EmitResponse(command_id, "session.connectAndJoin", true, 0, {},
                 response);
  }
  EmitNotification("session.ready", response);
  if (!audio_requested) {
    LOG_INFO("join finalized without audio backend=none");
    EmitNotification("media.disabled", Json{{"reason", "backend_none"}});
    return;
  }
  if (!initialized) {
    LOG_ERR("audio media initialization failed error={}", error);
    EmitMediaError(1710, "音频媒体初始化失败", error);
    return;
  }
  LOG_INFO("join finalized media_enabled=true");
  StartMediaForCurrentMode(generation);
}

bool EngineCore::InitializeMedia(const Json& join_data,
                                 std::string* error,
                                 std::uint64_t generation) {
  if (!session_state_.IsCurrent(generation)) {
    if (error) *error = "会话已失效";
    return false;
  }
  ResetMedia();
  webrtc_media::MediaConfig config;
  webrtc_media::MediaStatus status;
  SessionMediaMode mode = session_state_.media_mode();
  if (mode == SessionMediaMode::Unknown) {
    mode = SessionMediaMode::Sfu;
    if (!session_state_.SetMediaMode(generation, mode)) {
      if (error) *error = "会话已失效";
      return false;
    }
  }
  {
    std::lock_guard<std::mutex> lock(session_mutex_);
    config = media_config_;
    const Json room = join_data.value("room", Json::object());
    const Json policy = room.value("policy", Json::object());
    const Json video = policy.value("video", Json::object());
    config.camera_publish_enabled = video.value("enabled", true);
  }
  if (mode == SessionMediaMode::P2p && join_data.contains("p2p") &&
      join_data["p2p"].is_object()) {
    const Json& p2p = join_data["p2p"];
    p2p_session_ = p2p.value("session", std::string());
    p2p_peer_ = p2p.value("peer", std::string());
    p2p_offerer_ = p2p.value("role", std::string()) == "offerer";
  }
  media_session_ = std::make_unique<webrtc_media::MediaSession>();
  webrtc_media::MediaError media_error;
  const bool ok = media_session_->Initialize(
      config,
      [this, generation](std::string method, std::string data) {
        PostToWorker([this, method = std::move(method),
                      data = std::move(data), generation] {
          HandleMediaEvent(method, data, generation);
        });
      },
      [this, generation](webrtc_media::RemoteVideoFrame frame) {
        OnRemoteVideoFrame(frame, generation);
      },
      &media_error);
  if (!ok) {
    if (error) *error = media_error.message;
    media_session_.reset();
    return false;
  }
  if (!session_state_.IsCurrent(generation)) {
    media_session_->Shutdown();
    media_session_.reset();
    if (error) *error = "会话已失效";
    return false;
  }
  status = media_session_->status();
  EmitNotification("media.ready",
                   Json{{"recording_device", status.recording_device},
                        {"playout_device", status.playout_device}});
  LOG_INFO("media ready source={} recording_device={} playout_device={}",
           config.audio_source, status.recording_device, status.playout_device);
  return true;
}

void EngineCore::StartMediaForCurrentMode(std::uint64_t generation) {
  if (!session_state_.IsCurrent(generation) ||
      !session_state_.media_ready()) {
    return;
  }
  if (session_state_.media_mode() == SessionMediaMode::P2p) {
    StartP2p(generation);
  } else {
    PublishSfu(generation);
  }
}
void EngineCore::PublishSfu(std::uint64_t generation) {
  if (!session_state_.IsCurrent(generation)) return;
  if (!media_session_) {
    EmitMediaError(1730, "音频媒体未就绪", "SFU 发布被跳过");
    return;
  }
  webrtc_media::MediaSfuOffer offer;
  webrtc_media::MediaError error;
  if (!media_session_->CreateSfuPublishOffer(&offer, &error)) {
    LOG_ERR("sfu offer failed code={} error={}", error.code, error.message);
    if (!pending_sfu_request_id_.empty()) {
      FailPendingSfuSwitch(error.code == 0 ? 1730 : error.code,
                           error.message.empty()
                               ? "生成 SFU 音频 offer 失败"
                               : error.message,
                           generation);
    } else {
      EmitMediaError(error.code, "生成 SFU 音频 offer 失败", error.message);
    }
    return;
  }
  Json tracks = Json::array();
  tracks.push_back({{"mid", offer.audio_mid},
                    {"kind", "audio"},
                    {"source", offer.source}});
  if (!offer.camera_mid.empty()) {
    tracks.push_back({{"mid", offer.camera_mid},
                      {"kind", "video"},
                      {"source", "camera"}});
  }
  Json request = {{"sdp", offer.sdp}, {"tracks", tracks}};
  LOG_INFO("sfu publish offer source={} audio_mid={} camera_mid={} "
           "sdp_bytes={}",
           offer.source, offer.audio_mid, offer.camera_mid, offer.sdp.size());
  signaling_transport_.Request(
        "sfu.publish", request, [this, generation](Json result) {
          if (!session_state_.IsCurrent(generation)) return;
          if (!result.value("ok", false)) {
            const int code = ResponseCode(result);
            const std::string message = ResponseMessage(result);
            LOG_ERR("sfu publish rejected code={} message={}", code, message);
            PostToWorker([this, generation, code, message] {
              if (!session_state_.IsCurrent(generation)) return;
              if (!pending_sfu_request_id_.empty()) {
                FailPendingSfuSwitch(code, message, generation);
              } else {
                EmitMediaError(code, "SFU 音频发布失败", message);
              }
            });
            return;
          }
          const std::string answer = result.value("data", Json::object())
                                         .value("sdp", std::string());
          PostToWorker([this, generation, answer] {
            if (!session_state_.IsCurrent(generation)) return;
            ApplySfuAnswer(answer, generation);
          });
        });
  EmitNotification("media.publishing",
                   Json{{"mode", "sfu"}, {"sdp_bytes", offer.sdp.size()}});
}

void EngineCore::ApplySfuAnswer(const std::string& sdp,
                                std::uint64_t generation) {
  if (!session_state_.IsCurrent(generation) || !media_session_) return;
  if (sdp.empty()) {
    if (!pending_sfu_request_id_.empty()) {
      FailPendingSfuSwitch(1731, "SFU publish answer 为空", generation);
    } else {
      EmitMediaError(1731, "应用 SFU answer 失败", "SDP 为空");
    }
    return;
  }
  webrtc_media::MediaError error;
  if (!media_session_->ApplySfuPublishAnswer(sdp, &error)) {
    if (!pending_sfu_request_id_.empty()) {
      FailPendingSfuSwitch(error.code, error.message, generation);
    } else {
      EmitMediaError(error.code, "应用 SFU answer 失败", error.message);
    }
    return;
  }
  sfu_publish_ready_ = true;
  sfu_paused_ = !p2p_session_.empty();
  LOG_INFO("sfu publish answer applied sdp_bytes={} paused={}", sdp.size(),
           sfu_paused_);
  if (!media_session_->SetSfuPaused(sfu_paused_, &error)) {
    EmitMediaError(error.code, "更新 SFU 暂停状态失败", error.message);
  }
  EmitNotification("media.published", Json{{"ok", true}, {"mode", "sfu"}});
  if (!pending_sfu_request_id_.empty()) {
    TryCompleteSfuSwitch(false, generation);
  }
}

void EngineCore::StartP2p(std::uint64_t generation) {
  if (!session_state_.IsCurrent(generation) || !media_session_) {
    EmitMediaError(1731, "音频媒体未就绪", "P2P 初始化被跳过");
    return;
  }
  if (p2p_session_.empty() || p2p_peer_.empty()) {
    LOG_WARN("p2p allocation missing, falling back to sfu");
    EmitNotification(
        "p2p.state",
        Json{{"state", "failed"}, {"reason", "missing_allocation"}});
    EmitMediaError(1732, "P2P 分配参数缺失", "回退到 SFU 音频");
    if (!session_state_.SetMediaMode(generation, SessionMediaMode::Sfu)) {
      return;
    }
    PublishSfu(generation);
    return;
  }
  if (!p2p_offerer_) {
    LOG_INFO("p2p awaiting offer session={} peer={}", p2p_session_, p2p_peer_);
    EmitNotification("p2p.state", Json{{"session", p2p_session_},
                                      {"state", "awaiting_offer"}});
    return;
  }
  std::string offer;
  webrtc_media::MediaError error;
  if (!media_session_->CreateP2pOffer(&offer, &error)) {
    LOG_ERR("p2p offer failed code={} error={}", error.code, error.message);
    EmitMediaError(error.code, "创建 P2P 音频 offer 失败", error.message);
    EmitNotification("p2p.state",
                     Json{{"session", p2p_session_}, {"state", "failed"}});
    return;
  }
  Json event = {{"session", p2p_session_}, {"to", p2p_peer_}, {"sdp", offer}};
  LOG_INFO("p2p offer sent session={} peer={} sdp_bytes={}", p2p_session_,
           p2p_peer_, offer.size());
  signaling_transport_.Notify("p2p.offer", event);
  EmitNotification("p2p.started", Json{{"session", p2p_session_},
                                      {"peer", p2p_peer_},
                                      {"role", "offerer"}});
}

void EngineCore::ApplyP2pOffer(const std::string& session,
                               const std::string& from,
                               const std::string& sdp,
                               std::uint64_t generation) {
  if (!session_state_.IsCurrent(generation) || session.empty() ||
      from.empty() || sdp.empty()) {
    return;
  }
  LOG_INFO("p2p offer received session={} from={} sdp_bytes={}", session, from,
           sdp.size());
  if (p2p_session_.empty()) {
    p2p_session_ = session;
    p2p_peer_ = from;
    p2p_offerer_ = false;
    session_state_.SetMediaMode(generation, SessionMediaMode::P2p);
  }
  if (session != p2p_session_ || !media_session_) return;
  p2p_peer_ = from;
  p2p_offerer_ = false;
  std::string answer;
  webrtc_media::MediaError error;
  if (!media_session_->CreateP2pAnswer(sdp, &answer, &error)) {
    LOG_ERR("p2p answer create failed session={} code={} error={}", session,
              error.code, error.message);
    EmitMediaError(error.code, "创建 P2P 音频 answer 失败", error.message);
    EmitNotification("p2p.state",
                     Json{{"session", session}, {"state", "failed"}});
    return;
  }
  Json event = {{"session", session}, {"to", from}, {"sdp", answer}};
  signaling_transport_.Notify("p2p.answer", event);
  EmitNotification("p2p.started", Json{{"session", session},
                                      {"peer", from},
                                      {"role", "answerer"}});
}

void EngineCore::ApplyP2pAnswer(const std::string& session,
                                const std::string& sdp,
                                std::uint64_t generation) {
  if (!session_state_.IsCurrent(generation) || session != p2p_session_ ||
      !p2p_offerer_ || sdp.empty() || !media_session_) {
    return;
  }
  webrtc_media::MediaError error;
  if (!media_session_->ApplyP2pAnswer(sdp, &error)) {
    LOG_ERR("p2p answer apply failed session={} code={} error={}", session,
              error.code, error.message);
    EmitMediaError(error.code, "应用 P2P 音频 answer 失败", error.message);
    EmitNotification("p2p.state",
                     Json{{"session", session}, {"state", "failed"}});
  }
}

void EngineCore::ApplyP2pCandidate(const std::string& session,
                                   const std::string& candidate,
                                   const std::string& mid, int index,
                                   std::uint64_t generation) {
  if (!session_state_.IsCurrent(generation) ||
      session != p2p_session_ || !media_session_ || candidate.empty()) {
    return;
  }
  webrtc_media::MediaError error;
  if (!media_session_->AddP2pCandidate(candidate, mid, index, &error)) {
    EmitMediaError(error.code, "添加 P2P ICE 候选失败", error.message);
  }
}

void EngineCore::HandleMediaModeRequest(const std::string& request_id,
                                        const Json& data,
                                        std::uint64_t generation) {
  PostToWorker([this, request_id, data, generation] {
    if (!session_state_.IsCurrent(generation)) return;
    const std::string mode =
        data.value("mode", data.value("media_mode", std::string()));
    LOG_INFO("media mode request id={} mode={}", request_id, mode);
    if (!media_session_) {
      signaling_transport_.Send(
          signaling::Failure(request_id, 409, "音频媒体未就绪"));
      return;
    }
    if (mode == "p2p") {
      const Json allocation = data.value("p2p", Json::object());
      if (!allocation.is_object() || allocation.empty()) {
        signaling_transport_.Send(
            signaling::Failure(request_id, 400, "缺少 P2P 分配"));
        return;
      }
      pending_sfu_request_id_.clear();
      pending_sfu_room_mode_ = Json::object();
      sfu_publish_ready_ = false;
      sfu_subscribe_ready_ = false;
      PostToSignaling([this] { media_mode_timer_.cancel(); });
      p2p_session_ = allocation.value("session", std::string());
      p2p_peer_ = allocation.value("peer", std::string());
      p2p_offerer_ = allocation.value("role", std::string()) == "offerer";
      if (!session_state_.SetMediaMode(generation, SessionMediaMode::P2p)) {
        signaling_transport_.Send(
            signaling::Failure(request_id, 409, "会话已失效"));
        return;
      }
      StartP2p(generation);
      EmitNotification("room.mediaMode", data);
      signaling_transport_.Send(
          signaling::Response(request_id, Json::object()));
      return;
    }
    if (mode == "sfu") {
      pending_sfu_request_id_ = request_id;
      pending_sfu_room_mode_ = data;
      sfu_publish_ready_ = false;
      sfu_subscribe_ready_ = false;
      // Keep P2P live until publish and subscribe are both ready. The SFU
      // sender remains paused while the existing P2P link is active.
      PublishSfu(generation);
      if (pending_sfu_request_id_ == request_id) {
        ScheduleSfuSwitchTimeout(request_id, generation);
      }
      return;
    }
    signaling_transport_.Send(
        signaling::Failure(request_id, 400, "媒体模式无效"));
  });
}

void EngineCore::HandleSfuOfferRequest(const std::string& request_id,
                                       const Json& data,
                                       std::uint64_t generation) {
  const std::string sdp = data.value("sdp", std::string());
  if (sdp.empty()) {
    signaling_transport_.Send(signaling::Failure(request_id, 400, "缺少 SDP"));
    return;
  }
  const auto consumers = ParseRemoteTracks(data.value("consumers", Json::array()));
  PostToWorker([this, request_id, sdp, consumers, generation] {
    if (!session_state_.IsCurrent(generation)) return;
    if (!media_session_) {
      signaling_transport_.Send(
          signaling::Failure(request_id, 409, "音频媒体未就绪"));
      return;
    }
    std::string answer;
    webrtc_media::MediaError error;
    const bool ok =
        media_session_->CreateSfuSubscribeAnswer(sdp, consumers, &answer, &error);
    if (ok) {
      for (const auto& consumer : consumers) {
        Json track = {{"pc", "sfu-recv"},
                      {"mid", consumer.mid},
                      {"uid", consumer.uid},
                      {"kind", consumer.kind},
                      {"source", consumer.source}};
        EmitNotification("media.track", track);
      }
    }
    if (ok) {
      signaling_transport_.Send(
          signaling::Response(request_id, Json{{"sdp", answer}}));
      PostToWorker([this, generation] {
        if (!session_state_.IsCurrent(generation)) return;
        sfu_subscribe_ready_ = true;
        if (!pending_sfu_request_id_.empty()) {
          TryCompleteSfuSwitch(false, generation);
        }
      });
    } else {
      signaling_transport_.Send(signaling::Failure(
          request_id, error.code == 0 ? 500 : error.code, error.message));
      PostToWorker([this, error, generation] {
        if (!session_state_.IsCurrent(generation)) return;
        if (!pending_sfu_request_id_.empty()) {
          FailPendingSfuSwitch(error.code == 0 ? 500 : error.code,
                               error.message.empty()
                                   ? "SFU subscribe 失败"
                                   : error.message,
                               generation);
        }
      });
    }
  });
}

void EngineCore::TryCompleteSfuSwitch(bool timed_out,
                                      std::uint64_t generation) {
  if (!session_state_.IsCurrent(generation)) return;
  if (pending_sfu_request_id_.empty()) return;
  if (timed_out) {
    FailPendingSfuSwitch(504, "SFU 切换超时，保留当前 P2P 音频",
                         generation);
    return;
  }
  if (!sfu_publish_ready_ || !sfu_subscribe_ready_) return;

  const std::string request_id = pending_sfu_request_id_;
  const Json room_mode = pending_sfu_room_mode_;
  pending_sfu_request_id_.clear();
  pending_sfu_room_mode_ = Json::object();
  sfu_publish_ready_ = false;
  sfu_subscribe_ready_ = false;
  PostToSignaling([this] { media_mode_timer_.cancel(); });

  if (media_session_) media_session_->CloseP2p();
  p2p_session_.clear();
  p2p_peer_.clear();
  p2p_offerer_ = false;
  sfu_paused_ = false;
  webrtc_media::MediaError error;
  if (media_session_ && !media_session_->SetSfuPaused(false, &error)) {
    EmitMediaError(error.code, "恢复 SFU 音频失败", error.message);
  }
  if (!session_state_.SetMediaMode(generation, SessionMediaMode::Sfu)) return;
  EmitNotification("room.mediaMode",
                   room_mode.empty() ? Json{{"mode", "sfu"}} : room_mode);
  signaling_transport_.Send(
      signaling::Response(request_id, Json::object()));
}

void EngineCore::FailPendingSfuSwitch(int code, const std::string& message,
                                      std::uint64_t generation) {
  if (!session_state_.IsCurrent(generation)) return;
  if (pending_sfu_request_id_.empty()) return;
  const std::string request_id = pending_sfu_request_id_;
  pending_sfu_request_id_.clear();
  pending_sfu_room_mode_ = Json::object();
  sfu_publish_ready_ = false;
  sfu_subscribe_ready_ = false;
  PostToSignaling([this] { media_mode_timer_.cancel(); });

  if (!p2p_session_.empty()) {
    sfu_paused_ = true;
    if (media_session_) {
      webrtc_media::MediaError ignored;
      media_session_->SetSfuPaused(true, &ignored);
    }
  }
  EmitMediaError(code == 0 ? 1732 : code, "SFU 媒体切换失败", message);
  signaling_transport_.Send(signaling::Failure(
      request_id, code == 0 ? 500 : code,
      message.empty() ? "SFU 媒体切换失败" : message));
}

void EngineCore::ScheduleSfuSwitchTimeout(const std::string& request_id,
                                          std::uint64_t generation) {
  PostToSignaling([this, request_id, generation] {
    media_mode_timer_.expires_after(std::chrono::seconds(5));
    media_mode_timer_.async_wait([this, request_id, generation](
                                     const asio::error_code& error) {
      if (error) return;
      PostToWorker([this, request_id, generation] {
        if (session_state_.IsCurrent(generation) &&
            pending_sfu_request_id_ == request_id) {
          TryCompleteSfuSwitch(true, generation);
        }
      });
    });
  });
}
void EngineCore::HandleMediaEvent(const std::string& method,
                                  const std::string& data_json,
                                  std::uint64_t generation) {
  if (!session_state_.IsCurrent(generation)) return;
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
    signaling_transport_.Notify("p2p.candidate", candidate);
    return;
  }
  if (method == "audio.connection") {
    const std::string pc = data.value("pc", std::string());
    const std::string state = data.value("state", std::string());
    LOG_INFO("audio connection pc={} state={}", pc, state);
    if (pc == "p2p") {
      if (state == "connected") {
        sfu_paused_ = true;
        if (media_session_) {
          webrtc_media::MediaError error;
          media_session_->SetSfuPaused(true, &error);
        }
      } else if (state == "failed" || state == "closed") {
        if (sfu_paused_) {
          sfu_paused_ = false;
          if (media_session_) {
            webrtc_media::MediaError error;
            media_session_->SetSfuPaused(false, &error);
          }
        }
      }
      Json out = {{"session", p2p_session_}, {"state", state}};
      signaling_transport_.Notify("p2p.state", out);
    }
    EmitNotification("media.state", data);
    return;
  }
  if (method == "media.track" || method == "audio.track") {
    if (data.value("pc", std::string()) == "p2p") {
      data["uid"] = p2p_peer_;
      if (data.value("kind", std::string()) == "video" &&
          !data.contains("source")) {
        data["source"] = "camera";
      }
    }
    EmitNotification("media.track", data);
  }
}

void EngineCore::ResetMedia() {
  camera_generation_.fetch_add(1);
  mute_generation_.fetch_add(1);
  camera_publishing_.store(false);
  camera_preview_active_ = false;
  camera_toggle_pending_ = false;
  StopCameraCaptureIfIdle();
  if (media_session_) {
    media_session_->Shutdown();
    media_session_.reset();
  }
  p2p_session_.clear();
  p2p_peer_.clear();
  p2p_offerer_ = false;
  sfu_paused_ = false;
  sfu_publish_ready_ = false;
  sfu_subscribe_ready_ = false;
  pending_sfu_request_id_.clear();
  pending_sfu_room_mode_ = Json::object();
}

void EngineCore::EmitMediaError(int code, const std::string& message,
                                const std::string& detail) {
  EmitNotification("media.error",
                   Json{{"code", code},
                        {"message", message},
                        {"detail", detail}});
}

void EngineCore::HandleMediaSetMute(const EngineCommand& command) {
  const Json& request = command.data;
  if (!request.is_object()) {
    EmitResponse(command.id, command.method, false, 400, "静音参数无效");
    return;
  }
  if (!request.contains("muted") || !request["muted"].is_boolean()) {
    EmitResponse(command.id, command.method, false, 400, "缺少 muted 参数");
    return;
  }
  const EngineSessionSnapshot session = session_state_.snapshot();
  const bool joined = session.phase == SessionPhase::Joined &&
                      session.media_enabled;
  if (!joined || !media_session_) {
    EmitResponse(command.id, command.method, false, 409, "音频媒体未就绪");
    return;
  }
  const bool muted = request["muted"].get<bool>();
  LOG_INFO("media.setMute requested muted={}", muted);
  const uint64_t generation = mute_generation_.fetch_add(1) + 1;
  webrtc_media::MediaError media_error;
  if (muted && !media_session_->SetMuted(true, &media_error)) {
    EmitResponse(command.id, command.method, false, media_error.code,
                 media_error.message);
    return;
  }
  const uint64_t command_id = command.id;
  const uint64_t session_generation = session.generation;
  signaling_transport_.Request(
      "media.setMute", {{"source", "mic"}, {"muted", muted}},
      [this, command_id, generation, session_generation, muted](Json result) {
          PostToWorker([this, command_id, generation, session_generation,
                        muted, result] {
            if (generation != mute_generation_.load()) {
              EmitResponse(command_id, "media.setMute", false, 487,
                           "静音命令已被更新");
              return;
            }
            if (!session_state_.IsCurrent(session_generation)) {
              EmitResponse(command_id, "media.setMute", false, 409,
                           "会话已失效");
              return;
            }
            const bool server_ok = result.value("ok", false);
            const bool effective = muted || !server_ok;
            webrtc_media::MediaError media_error;
            const bool applied =
                media_session_ &&
                media_session_->SetMuted(effective, &media_error);
            Json state = {{"muted", applied ? effective : true}};
            LOG_INFO(
                "media.setMute confirmed requested={} server_ok={} "
                "effective={} applied={} error={}",
                muted, server_ok, effective, applied, media_error.message);
            EmitNotification("media.muted", state);
            if (!server_ok) {
              EmitResponse(command_id, "media.setMute", false,
                           ResponseCode(result), ResponseMessage(result));
            } else if (!applied) {
              EmitResponse(command_id, "media.setMute", false,
                           media_error.code, media_error.message);
            } else {
              EmitResponse(command_id, "media.setMute", true, 0, {},
                           state);
            }
          });
      });
}

void EngineCore::HandleMediaToggleCamera(const EngineCommand& command) {
  const EngineSessionSnapshot session = session_state_.snapshot();
  const bool joined = session.phase == SessionPhase::Joined &&
                      session.media_enabled;
  if (!joined || !media_session_) {
    EmitResponse(command.id, command.method, false, 409, "媒体尚未就绪");
    return;
  }
  if (camera_toggle_pending_) {
    EmitResponse(command.id, command.method, false, 409,
                 "摄像头命令正在处理中");
    return;
  }

  const bool enable = !camera_publishing_.load();
  if (enable) {
    std::string error;
    if (!EnsureCameraCapture(&error)) {
      EmitResponse(command.id, command.method, false, 1501,
                   error.empty() ? "摄像头启动失败" : error);
      return;
    }
  } else {
    camera_publishing_.store(false);
    webrtc_media::MediaError media_error;
    media_session_->SetCameraEnabled(false, &media_error);
    StopCameraCaptureIfIdle();
  }

  camera_toggle_pending_ = true;
  const uint64_t generation = camera_generation_.fetch_add(1) + 1;
  const uint64_t command_id = command.id;
  const uint64_t session_generation = session.generation;
  LOG_INFO("media.toggleCamera generation={} enable={}", generation, enable);
  signaling_transport_.Request(
      "media.setMute", Json{{"source", "camera"}, {"muted", !enable}},
      [this, command_id, generation, session_generation, enable](Json result) {
          PostToWorker([this, command_id, generation, session_generation,
                        enable, result] {
            if (generation != camera_generation_.load()) {
              EmitResponse(command_id, "media.toggleCamera", false, 487,
                           "摄像头命令已被更新");
              return;
            }
            if (!session_state_.IsCurrent(session_generation)) {
              EmitResponse(command_id, "media.toggleCamera", false, 409,
                           "会话已失效");
              return;
            }
            camera_toggle_pending_ = false;
            const bool server_ok = result.value("ok", false);
            webrtc_media::MediaError media_error;
            bool applied = true;
            if (server_ok && enable) {
              camera_publishing_.store(true);
              applied = media_session_ &&
                        media_session_->SetCameraEnabled(true, &media_error);
            } else if (!enable) {
              camera_publishing_.store(false);
              applied = media_session_ &&
                        media_session_->SetCameraEnabled(false, &media_error);
            }
            if (!server_ok || !applied) {
              camera_publishing_.store(false);
              if (media_session_) {
                media_session_->SetCameraEnabled(false, &media_error);
              }
              if (!camera_preview_active_) camera_.Stop();
            }
            const bool effective = server_ok && applied && enable;
            Json state = {{"source", "camera"},
                          {"sharing", effective},
                          {"camera", effective}};
            EmitNotification("video.state", state);
            if (!server_ok) {
              EmitResponse(command_id, "media.toggleCamera", false,
                           ResponseCode(result), ResponseMessage(result));
            } else if (!applied) {
              EmitResponse(command_id, "media.toggleCamera", false,
                           media_error.code == 0 ? 1730 : media_error.code,
                           media_error.message.empty()
                               ? "应用摄像头发送状态失败"
                               : media_error.message);
            } else {
              EmitResponse(command_id, "media.toggleCamera", true, 0, {},
                           state);
            }
          });
      });
}

void EngineCore::HandleMediaToggleScreenShare(const EngineCommand& command) {
  EmitResponse(command.id, command.method, false, 501,
               "屏幕共享将在下一子阶段接入");
}

void EngineCore::HandleMediaSetVolume(const EngineCommand& command) {
  const Json& request = command.data;
  if (!request.is_object()) {
    EmitResponse(command.id, command.method, false, 400, "音量参数无效");
    return;
  }
  double gain = request.value("gain", -1.0);
  if (gain < 0.0 && request.contains("volume")) {
    gain = request.value("volume", 60) / 100.0;
  }
  if (!media_session_ ||
      !media_session_->SetVolume(gain, nullptr)) {
    EmitResponse(command.id, command.method, false, 400, "音量必须在 0 到 100 之间");
    return;
  }
  Json data = {{"volume", static_cast<int>(gain * 100.0 + 0.5)}};
  EmitResponse(command.id, command.method, true, 0, {}, data);
}

void EngineCore::HandleMediaSetDeafened(const EngineCommand& command) {
  const Json& request = command.data;
  if (!request.is_object()) {
    EmitResponse(command.id, command.method, false, 400, "收听参数无效");
    return;
  }
  const bool deafened = request.value("deafened", false);
  webrtc_media::MediaError error;
  if (!media_session_ || !media_session_->SetDeafened(deafened, &error)) {
    EmitResponse(command.id, command.method, false,
                 error.code == 0 ? 409 : error.code,
                 error.message.empty() ? "音频媒体未就绪" : error.message);
    return;
  }
  deafened_ = deafened;
  Json data = {{"deafened", deafened}};
  EmitNotification("media.deafened", data);
  EmitResponse(command.id, command.method, true, 0, {}, data);
}

void EngineCore::HandleMediaStats(const EngineCommand& command) {
  if (!media_session_) {
    EmitResponse(command.id, command.method, false, 409, "音频媒体未就绪");
    return;
  }
  const auto status = media_session_->status();
  LOG_INFO(
      "media stats source={} muted={} recording={} capture_frames={} "
      "sfu={}/{} p2p={}/{} sent={} recv={} remote_tracks={} level={} "
      "camera_frames={} camera_sender={} video_sent={} video_recv={} "
      "remote_video_tracks={}",
      media_config_.audio_source, status.muted, status.recording,
      status.capture_frames, status.sfu_send_state, status.sfu_recv_state,
      status.p2p_state, status.p2p_connected, status.p2p_audio_packets_sent,
      status.audio_packets_received, status.remote_tracks,
      status.remote_audio_level, status.camera_capture_frames,
      status.camera_sender_active, status.video_packets_sent,
      status.video_packets_received, status.remote_video_tracks);
  Json data = {
      {"initialized", status.initialized},
      {"sfu_send_ready", status.sfu_send_ready},
      {"sfu_recv_ready", status.sfu_recv_ready},
      {"p2p_ready", status.p2p_ready},
      {"p2p_connected", status.p2p_connected},
      {"muted", status.muted},
      {"deafened", status.deafened},
      {"sfu_paused", status.sfu_paused},
      {"camera_enabled", status.camera_enabled},
      {"recording_initialized", status.recording_initialized},
      {"recording", status.recording},
      {"playout_initialized", status.playout_initialized},
      {"playing", status.playing},
      {"sfu_send_active", status.sfu_send_active},
      {"p2p_send_active", status.p2p_send_active},
      {"camera_sender_active", status.camera_sender_active},
      {"volume", status.volume},
      {"capture_frames", status.capture_frames},
      {"camera_capture_frames", status.camera_capture_frames},
      {"remote_tracks", status.remote_tracks},
      {"active_remote_tracks", status.active_remote_tracks},
      {"remote_video_tracks", status.remote_video_tracks},
      {"active_remote_video_tracks", status.active_remote_video_tracks},
      {"sfu_audio_packets_sent", status.sfu_audio_packets_sent},
      {"p2p_audio_packets_sent", status.p2p_audio_packets_sent},
      {"video_packets_sent", status.video_packets_sent},
      {"video_packets_received", status.video_packets_received},
      {"audio_packets_received", status.audio_packets_received},
      {"audio_bytes_sent", status.audio_bytes_sent},
      {"audio_bytes_received", status.audio_bytes_received},
      {"video_bytes_sent", status.video_bytes_sent},
      {"video_bytes_received", status.video_bytes_received},
      {"remote_audio_level", status.remote_audio_level},
      {"recording_device", status.recording_device},
      {"playout_device", status.playout_device},
      {"sfu_send_state", status.sfu_send_state},
      {"sfu_recv_state", status.sfu_recv_state},
      {"p2p_state", status.p2p_state},
      {"last_error", status.last_error}};
  EmitResponse(command.id, command.method, true, 0, {}, data);
}
void EngineCore::HandleRoomLeave(const EngineCommand& command) {
  std::string room_id;
  const EngineSessionSnapshot session = session_state_.snapshot();
  const bool joined = session.phase == SessionPhase::Joined;
  {
    std::lock_guard<std::mutex> lock(session_mutex_);
    room_id = session_room_id_;
  }
  if (!joined || room_id.empty()) {
    EmitResponse(command.id, command.method, true, 0, {}, Json::object());
    return;
  }
  const uint64_t id = command.id;
  const uint64_t generation = session.generation;
  signaling_transport_.Request(
      "room.leave", {{"room_id", room_id}}, [this, id, generation](Json result) {
      if (!session_state_.IsCurrent(generation)) return;
      if (result.value("ok", false)) {
        if (!session_state_.MarkLeaving(generation)) return;
        PostToWorker([this, id, generation] {
          if (!session_state_.IsGeneration(generation)) return;
          session_state_.MarkDisconnected();
          ResetMedia();
          EmitResponse(id, "room.leave", true, 0, {}, Json::object());
        });
      } else {
        EmitResponse(id, "room.leave", false, ResponseCode(result),
                     ResponseMessage(result));
      }
      });
}

void EngineCore::HandleSessionDisconnect(const EngineCommand& command) {
  const EngineSessionSnapshot session = session_state_.snapshot();
  if (session.phase != SessionPhase::Disconnected) {
    if (session.phase == SessionPhase::Joined) {
      session_state_.MarkLeaving(session.generation);
    }
    session_state_.MarkDisconnected();
  }
  const uint64_t id = command.id;
  signaling_transport_.Post([this] {
    heartbeat_timer_.cancel();
    media_mode_timer_.cancel();
    signaling_transport_.Dispose();
  });
  PostToWorker([this, id] {
    ResetMedia();
    EmitResponse(id, "session.disconnect", true, 0, {}, Json::object());
    EmitNotification("session.closed", Json{{"reason", "local_disconnect"}});
  });
}

void EngineCore::ScheduleHeartbeat(int seconds,
                                   std::uint64_t generation) {
  if (!session_state_.IsCurrent(generation) ||
      !session_state_.signaling_ready()) {
    return;
  }
  heartbeat_seconds_ = std::clamp(seconds, 1, 30);
  heartbeat_timer_.expires_after(std::chrono::seconds(heartbeat_seconds_));
  heartbeat_timer_.async_wait([this, generation](
                                  const asio::error_code& error) {
    if (error) return;
    if (!session_state_.IsCurrent(generation) ||
        !session_state_.signaling_ready()) {
      return;
    }
    signaling_transport_.Request("session.ping", Json::object(),
                                 [this, generation](Json result) {
      if (!session_state_.IsCurrent(generation)) return;
      if (result.value("ok", false)) return;
      EmitNotification(
          "session.error",
          Json{{"code", ResponseCode(result)},
               {"message", ResponseMessage(result)}});
                                 });
    ScheduleHeartbeat(heartbeat_seconds_, generation);
  });
}

bool EngineCore::EnsureCameraCapture(std::string* error) {
  if (camera_.running()) return true;
  camera_win::CameraCaptureConfig config;
  config.device_index = camera_device_index_;
  return camera_.Start(
      config,
      [this](const camera_win::I420Frame& frame) { OnCameraFrame(frame); },
      [this](const std::string& message) {
        EmitNotification("camera.preview.error", Json{{"message", message}});
      },
      error);
}

void EngineCore::StopCameraCaptureIfIdle() {
  if (!camera_preview_active_ &&
      !camera_publishing_.load(std::memory_order_acquire)) {
    camera_.Stop();
  }
}

void EngineCore::OnCameraFrame(const camera_win::I420Frame& frame) {
  if (camera_publishing_.load(std::memory_order_acquire) &&
      media_session_ != nullptr) {
    webrtc_media::VideoFrameData data;
    data.width = frame.width;
    data.height = frame.height;
    data.stride_y = frame.stride_y;
    data.stride_u = frame.stride_u;
    data.stride_v = frame.stride_v;
    data.data_y = frame.y.data();
    data.data_u = frame.u.data();
    data.data_v = frame.v.data();
    data.timestamp_us = frame.timestamp_us;
    webrtc_media::MediaError error;
    if (!media_session_->PushCameraFrame(data, &error)) {
      EmitNotification("media.error",
                       Json{{"code", error.code},
                            {"message", "摄像头帧送入编码器失败"},
                            {"detail", error.message}});
    }
  }
  if (options_.on_local_frame == nullptr) return;
  std::string uid;
  {
    std::lock_guard<std::mutex> lock(session_mutex_);
    uid = session_uid_;
  }
  rtcwin_frame_view view{};
  view.uid = uid.c_str();
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

void EngineCore::OnRemoteVideoFrame(
    const webrtc_media::RemoteVideoFrame& frame,
    std::uint64_t generation) {
  if (!session_state_.IsCurrent(generation)) return;
  if (options_.on_remote_frame == nullptr) return;
  rtcwin_frame_view view{};
  view.uid = frame.uid.c_str();
  view.mid = frame.mid.c_str();
  view.i420_y = frame.y.data();
  view.i420_u = frame.u.data();
  view.i420_v = frame.v.data();
  view.width = frame.width;
  view.height = frame.height;
  view.stride_y = frame.stride_y;
  view.stride_u = frame.stride_u;
  view.stride_v = frame.stride_v;
  view.timestamp_us = frame.timestamp_us;
  view.source = frame.source == "screen" ? 1 : 0;
  options_.on_remote_frame(options_.userdata, &view);
}

void EngineCore::EmitResponse(uint64_t id, const std::string& method, bool ok,
                              int error_code, const std::string& message,
                              const Json& data) {
  events_.EmitResponse(id, method, ok, error_code, message, data);
}

void EngineCore::EmitNotification(const std::string& method,
                                  const Json& data) {
  events_.EmitNotification(method, data);
}

}  // namespace rtcwin::engine
