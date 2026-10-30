#include "rtcwin/engine/session_controller.h"

#include "engine_log.h"
#include "rtcwin/signaling/protocol.h"

#include <algorithm>
#include <chrono>
#include <mutex>
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

}  // namespace

struct SessionController::Impl {
  explicit Impl(Context context)
      : context(std::move(context)),
        heartbeat_timer(context.signaling.context()) {}

  void ConnectAndJoin(const EngineCommand& command) {
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
    const std::string media_mode =
        request.value("media_mode", std::string("auto"));
    const std::string talk_mode =
        request.value("talk_mode", std::string("free"));
    LOG_INFO(
        "join request uid={} room={} backend={} source={} mode={} talk={}",
        uid, room_id, request.value("audio_backend", std::string()),
        request.value("audio_source", std::string()), media_mode, talk_mode);
    if (url.rfind("ws://", 0) != 0 || token.empty() || uid.empty() ||
        room_id.empty()) {
      EmitResponse(command.id, command.method, false, 400,
                   "信令地址、Token、用户标识和房间 ID 不能为空");
      return;
    }
    if (media_mode != "auto" && media_mode != "sfu" &&
        media_mode != "p2p") {
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
      std::lock_guard<std::mutex> lock(mutex);
      room = room_id;
      display_name = display;
      talk = talk_mode;
    }
    const std::uint64_t generation = context.session.BeginConnect(command.id);
    context.media.Configure(media_config, uid);
    context.media.Reset();
    Notify("session.state", Json{{"state", "connecting"}});

    const auto fail_session = [this, generation](int code,
                                                 const std::string& message) {
      if (!context.session.IsCurrent(generation)) return;
      const std::uint64_t id = context.session.TakeJoinCommand(generation);
      context.session.MarkFailed(generation);
      if (id != 0) {
        EmitResponse(id, "session.connectAndJoin", false, code, message);
      }
      context.signaling.Post([this] { heartbeat_timer.cancel(); });
      context.post_to_worker([this, generation] {
        if (context.session.IsGeneration(generation)) context.media.Reset();
      });
    };

    signaling::SignalingClient::Handlers handlers;
    handlers.on_open = [this, generation, uid, room_id, display, media_mode,
                        talk_mode, audio_requested, fail_session] {
      if (!context.session.IsCurrent(generation)) return;
      Json hello = {
          {"client",
           {{"platform", "windows-x64"},
            {"version", "0.1.0"},
            {"sdk", "rtcwin-s4"}}},
          {"caps",
           {{"audio", {{"codecs", Json::array({"opus"})}, {"aec", "webrtc"}}},
            {"p2p", true},
            {"p2p_video", true},
            {"video",
             {{"codecs",
               Json::array({{{"name", "h264"}, {"hw", false}}})}}},
            {"sfu_sdp", "unified-plan"}}}};
      context.signaling.Request(
          "session.hello", hello,
          [this, generation, uid, room_id, display, media_mode, talk_mode,
           audio_requested, fail_session](Json result) {
            if (!context.session.IsCurrent(generation)) return;
            if (!result.value("ok", false)) {
              fail_session(ResponseCode(result), ResponseMessage(result));
              return;
            }
            const Json hello_data = result.value("data", Json::object());
            LOG_INFO(
                "session hello ok session={} ice_servers={} heartbeat={}",
                hello_data.value("session_id", std::string()),
                hello_data.value("ice_servers", Json::array()).size(),
                hello_data.value("heartbeat_interval", 15));
            context.media.SetIceServers(ParseIceServers(
                hello_data.value("ice_servers", Json::array())));
            if (!context.session.MarkSignalingReady(generation)) return;
            ScheduleHeartbeat(
                hello_data.value("heartbeat_interval", 15), generation);
            if (!context.session.MarkJoiningRoom(generation)) return;
            Json join = {
                {"room_id", room_id},
                {"display", display.empty() ? uid : display},
                {"media",
                 {{"audio", {{"send", audio_requested}, {"muted", true}}},
                  {"video", {{"send", false}}}}},
                {"policy",
                 {{"talk_mode", talk_mode}, {"media_mode", media_mode}}}};
            context.signaling.Request(
                "room.join", join,
                [this, generation, room_id, media_mode,
                 fail_session](Json joined) {
                  if (!context.session.IsCurrent(generation)) return;
                  if (!joined.value("ok", false)) {
                    fail_session(ResponseCode(joined),
                                 ResponseMessage(joined));
                    return;
                  }
                  const Json data = joined.value("data", Json::object());
                  LOG_INFO("room join ok room={} media_mode={} peers={}",
                           room_id,
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
                  if (!context.session.MarkJoined(generation, selected_mode,
                                                  false)) {
                    return;
                  }
                  const std::uint64_t id =
                      context.session.TakeJoinCommand(generation);
                  context.post_to_worker(
                      [this, id, data, generation] {
                        FinalizeJoin(id, data, generation);
                      });
                });
          });
    };

    handlers.on_message = [this, generation](Json message) {
      if (!context.session.IsCurrent(generation)) return;
      const std::string type = message.value("type", std::string());
      const std::string method = message.value("method", std::string());
      const Json data = message.value("data", Json::object());
      if (type == "req") {
        const std::string request_id = message.value("id", std::string());
        if (method == "room.mediaMode") {
          context.media.HandleMediaModeRequest(request_id, data, generation);
        } else if (method == "sfu.offer") {
          context.media.HandleSfuOfferRequest(request_id, data, generation);
        } else {
          context.signaling.Send(signaling::Failure(
              request_id, 501, "该客户端尚不支持此请求"));
        }
        return;
      }
      if (context.media.HandleSignalingEvent(method, data, generation)) return;
      if (!method.empty() && message.contains("data")) {
        if (method == "room.peerUpdated" ||
            method == "room.peerJoined") {
          const Json audio = data.value("audio", Json::object());
          LOG_INFO("room peer audio method={} uid={} muted={} speaking={}",
                   method, data.value("uid", std::string()),
                   audio.value("muted", true),
                   audio.value("speaking", false));
        }
        Notify(method, data);
      }
    };

    handlers.on_closed = [this, generation](
                             const signaling::SignalingClient::CloseInfo& info) {
      if (!context.session.IsCurrent(generation)) return;
      const std::uint64_t id = context.session.TakeJoinCommand(generation);
      context.session.MarkFailed(generation);
      if (id != 0) {
        EmitResponse(id, "session.connectAndJoin", false,
                     info.status != 0 ? info.status : 480, "信令连接关闭");
      }
      heartbeat_timer.cancel();
      context.post_to_worker([this, generation] {
        if (context.session.IsGeneration(generation)) context.media.Reset();
      });
      Notify("session.closed",
             Json{{"handshake_failed", info.handshake_failed},
                  {"fatal", info.fatal},
                  {"status", info.status},
                  {"code", info.code}});
    };

    handlers.on_error = [this, generation](int code, std::string message) {
      if (!context.session.IsCurrent(generation)) return;
      Notify("session.error",
             Json{{"code", code}, {"message", std::move(message)}});
    };

    context.signaling.Connect(
        url, token, std::move(handlers),
        [fail_session](int code, std::string message) {
          fail_session(code, std::move(message));
        });
  }

  void Leave(const EngineCommand& command) {
    std::string current_room;
    const EngineSessionSnapshot session = context.session.snapshot();
    {
      std::lock_guard<std::mutex> lock(mutex);
      current_room = room;
    }
    if (session.phase != SessionPhase::Joined || current_room.empty()) {
      EmitResponse(command.id, command.method, true, 0, {},
                   Json::object());
      return;
    }
    const std::uint64_t generation = session.generation;
    context.signaling.Request(
        "room.leave", {{"room_id", current_room}},
        [this, id = command.id, generation](Json result) {
          if (!context.session.IsCurrent(generation)) return;
          if (!result.value("ok", false)) {
            EmitResponse(id, "room.leave", false, ResponseCode(result),
                         ResponseMessage(result));
            return;
          }
          if (!context.session.MarkLeaving(generation)) return;
          context.post_to_worker([this, id, generation] {
            if (!context.session.IsGeneration(generation)) return;
            context.session.MarkDisconnected();
            context.media.Reset();
            EmitResponse(id, "room.leave", true, 0, {}, Json::object());
          });
        });
  }

  void Disconnect(const EngineCommand& command) {
    const EngineSessionSnapshot session = context.session.snapshot();
    if (session.phase != SessionPhase::Disconnected) {
      if (session.phase == SessionPhase::Joined) {
        context.session.MarkLeaving(session.generation);
      }
      context.session.MarkDisconnected();
    }
    context.signaling.Post([this] {
      heartbeat_timer.cancel();
      context.signaling.Dispose();
    });
    context.post_to_worker([this, command] {
      context.media.Reset();
      EmitResponse(command.id, "session.disconnect", true, 0, {},
                   Json::object());
      Notify("session.closed", Json{{"reason", "local_disconnect"}});
    });
  }

  void CancelHeartbeat() { heartbeat_timer.cancel(); }

 private:
  void FinalizeJoin(std::uint64_t command_id, const Json& data,
                    std::uint64_t generation) {
    if (!context.session.IsCurrent(generation) ||
        context.session.snapshot().phase != SessionPhase::Joined) {
      return;
    }
    const bool audio_requested = context.media.audio_requested();
    std::string error;
    bool initialized = false;
    if (audio_requested) {
      webrtc_media::MediaError media_error;
      initialized =
          context.media.Initialize(data, generation, &media_error);
      error = media_error.message;
    }
    if (!context.session.SetMediaEnabled(generation, initialized)) return;
    Json response = data;
    response["media_enabled"] = initialized;
    if (command_id != 0) {
      EmitResponse(command_id, "session.connectAndJoin", true, 0, {},
                   response);
    }
    Notify("session.ready", response);
    if (!audio_requested) {
      LOG_INFO("join finalized without audio backend=none");
      Notify("media.disabled", Json{{"reason", "backend_none"}});
      return;
    }
    if (!initialized) {
      LOG_ERR("audio media initialization failed error={}", error);
      Notify("media.error",
             Json{{"code", 1710},
                  {"message", "音频媒体初始化失败"},
                  {"detail", error}});
      return;
    }
    LOG_INFO("join finalized media_enabled=true");
    context.media.Start(generation);
  }

  void ScheduleHeartbeat(int seconds, std::uint64_t generation) {
    if (!context.session.IsCurrent(generation) ||
        !context.session.signaling_ready()) {
      return;
    }
    heartbeat_seconds = std::clamp(seconds, 1, 30);
    heartbeat_timer.expires_after(std::chrono::seconds(heartbeat_seconds));
    heartbeat_timer.async_wait([this, generation](
                                   const asio::error_code& timer_error) {
      if (timer_error) return;
      if (!context.session.IsCurrent(generation) ||
          !context.session.signaling_ready()) {
        return;
      }
      context.signaling.Request(
          "session.ping", Json::object(), [this, generation](Json result) {
            if (!context.session.IsCurrent(generation)) return;
            if (result.value("ok", false)) return;
            Notify("session.error",
                   Json{{"code", ResponseCode(result)},
                        {"message", ResponseMessage(result)}});
          });
      ScheduleHeartbeat(heartbeat_seconds, generation);
    });
  }

  void EmitResponse(std::uint64_t id, const std::string& method, bool ok,
                    int code, const std::string& message,
                    const Json& data = Json::object()) {
    context.events.EmitResponse(id, method, ok, code, message, data);
  }

  void Notify(const std::string& method, const Json& data) {
    context.events.EmitNotification(method, data);
  }

  Context context;
  std::mutex mutex;
  std::string room;
  std::string display_name;
  std::string talk;
  int heartbeat_seconds = 15;
  asio::steady_timer heartbeat_timer;
};

SessionController::SessionController(Context context)
    : impl_(std::make_unique<Impl>(std::move(context))) {}

SessionController::~SessionController() = default;

void SessionController::ConnectAndJoin(const EngineCommand& command) {
  impl_->ConnectAndJoin(command);
}

void SessionController::Leave(const EngineCommand& command) {
  impl_->Leave(command);
}

void SessionController::Disconnect(const EngineCommand& command) {
  impl_->Disconnect(command);
}

void SessionController::CancelHeartbeat() { impl_->CancelHeartbeat(); }

}  // namespace rtcwin::engine
