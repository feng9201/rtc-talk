// File: core/session/call_session.cpp
// Brief: 实现信令会话状态机与命令路由。
#include "session/call_session.h"

#include <algorithm>
#include <chrono>
#include <stdexcept>
#include <utility>

#include "base/log.h"
#include "signaling/protocol.h"
#include "talk/talk.h"

namespace talk {
namespace {

// 媒体结果转换为协议响应；id 由调用方补全。
Json ToResponse(const MediaResult& result) {
  if (result.ok) {
    return Response("", result.data);
  }
  return Failure("", result.code, result.error);
}

int Clamp(int value, int low, int high) {
  return std::max(low, std::min(high, value));
}

// floor 视图里的 holder 可能是 null，直接 value() 会抛类型错误。
std::string HolderOf(const Json& floor) {
  if (!floor.is_object() || !floor.contains("holder") ||
      !floor["holder"].is_string()) {
    return {};
  }
  return floor["holder"].get<std::string>();
}

}  // namespace

CallSession::CallSession(asio::io_context& io, const Json& config,
                         EventSink& events, SignalingClient& signaling,
                         MediaController& media)
    : io_(io),
      config_(config),
      events_(events),
      signaling_(signaling),
      media_(media),
      heartbeat_(io),
      reconnect_(io) {}

CallSession::~CallSession() = default;

void CallSession::Command(uint64_t id, std::string method, Json data) {
  if (stopping_) {
    TALK_LOG_DEBUG("session", "已停止，忽略命令 id={} method={}", id,
                   method);
    return;
  }
  if (method == "auth.update") {
    HandleAuthUpdate(id, method, std::move(data));
    return;
  }
  if (method == "connect") {
    HandleConnect(id, method, std::move(data));
    return;
  }
  if (method == "disconnect" || method == "session.bye") {
    HandleDisconnect(id, method);
    return;
  }
  if (disconnecting_) {
    Complete(id, method, Failure("", 409, "正在退出"));
    return;
  }
  if (method == "media.stats" || method == "audio.devices" ||
      method == "audio.volume" || method == "audio.setSource" ||
      method == "camera.devices" || method == "video.previewStart" ||
      method == "video.previewStop") {
    HandleMediaCommand(id, method, std::move(data));
    return;
  }
  if (!ready_) {
    Complete(id, method, Failure("", 409, "尚未完成 hello"));
    return;
  }
  if (method == "room.join") {
    HandleJoin(id, method, std::move(data));
    return;
  }
  if (method == "room.leave") {
    HandleLeave(id, method);
    return;
  }
  if (method == "media.setMute") {
    HandleSetMute(id, method, std::move(data));
    return;
  }
  if (method == "floor.request" || method == "floor.release" ||
      method == "host.grant" || method == "host.muteAll" ||
      method == "host.kick" || method == "host.setPolicy" ||
      method == "room.subscribeLevels") {
    HandleFloorCommand(id, method, std::move(data));
    return;
  }
  if (method == "video.start") {
    HandleVideoStart(id, method, std::move(data));
    return;
  }
  if (method == "video.stop") {
    HandleVideoStop(id, method, std::move(data));
    return;
  }
  if (method == "session.ping" || method == "session.refreshIce") {
    HandleSessionRequest(id, method, std::move(data));
    return;
  }
  TALK_LOG_WARN("session", "未知命令 id={} method={}，返回 501", id,
                method);
  Complete(id, method, Failure("", 501, "功能尚未实现"));
}

void CallSession::HandleAuthUpdate(uint64_t id, const std::string& method,
                                   Json data) {
  const std::string token = data.at("token").get<std::string>();
  if (token.empty() || token.find_first_of("\r\n") != std::string::npos) {
    throw std::runtime_error("token 无效");
  }
  token_ = token;
  Complete(id, method, Response("", Json::object()));
}

void CallSession::HandleConnect(uint64_t id, const std::string& method,
                                Json data) {
  if (retry_ || ready_ || disconnecting_) {
    Complete(id, method, Failure("", 409, "已经连接或正在退出"));
    return;
  }
  url_ = data.at("url").get<std::string>();
  token_ = data.at("token").get<std::string>();
  if (url_.rfind("ws://", 0) != 0 || url_.find('?') != std::string::npos ||
      token_.empty() ||
      token_.find_first_of("\r\n") != std::string::npos) {
    throw std::runtime_error("连接参数无效");
  }
  retry_ = true;
  TALK_LOG_INFO("session", "开始连接 url={}", url_);
  StartConnect(id, false);
}

void CallSession::HandleDisconnect(uint64_t id, const std::string& method) {
  if (disconnecting_) {
    Complete(id, method, Failure("", 409, "正在退出"));
    return;
  }
  TALK_LOG_INFO("session", "断开连接 ready={} room={}", ready_,
                room_id_);
  retry_ = false;
  reconnect_.cancel();
  ResetMedia();
  if (!ready_) {
    HandleLost();
    Complete(id, method, Response("", Json::object()));
    return;
  }
  disconnecting_ = true;
  const uint64_t epoch = epoch_;
  signaling_.Request(
      "session.bye", Json::object(), [this, id, method, epoch](Json result) {
        if (epoch == epoch_) {
          HandleLost();
        }
        Complete(id, method, result);
      });
}

void CallSession::HandleMediaCommand(uint64_t id, const std::string& method,
                                     Json data) {
  if (method == "media.stats") {
    media_.GetStats([this, id, method](MediaResult result) {
      Complete(id, method, ToResponse(result));
    });
  } else if (method == "camera.devices") {
    media_.GetCameras([this, id, method](MediaResult result) {
      Complete(id, method, ToResponse(result));
    });
  } else if (method == "video.previewStart") {
    const int camera_index =
        data.value("camera_index", -1);
    media_.StartCameraPreview(
        camera_index, [this, id, method](MediaResult result) {
          Complete(id, method, ToResponse(result));
        });
  } else if (method == "video.previewStop") {
    media_.StopCameraPreview([this, id, method](MediaResult result) {
      Complete(id, method, ToResponse(result));
    });
  } else if (method == "audio.devices") {
    media_.GetDevices([this, id, method](MediaResult result) {
      Complete(id, method, ToResponse(result));
    });
  } else if (method == "audio.setSource") {
    if (!data.contains("source") || !data["source"].is_string()) {
      Complete(id, method, Failure("", 400, "source 必须是字符串"));
      return;
    }
    const std::string source = data["source"].get<std::string>();
    if (source != "mic" && source != "system") {
      Complete(id, method, Failure("", 400, "source 必须是 mic/system"));
      return;
    }
    media_.SetAudioSource(source, [this, id, method](MediaResult result) {
      Complete(id, method, ToResponse(result));
    });
  } else {
    const double gain = data.at("gain").get<double>();
    media_.SetVolume(gain, [this, id, method](MediaResult result) {
      Complete(id, method, ToResponse(result));
    });
  }
}

void CallSession::HandleJoin(uint64_t id, const std::string& method,
                             Json data) {
  if (!room_id_.empty() || joining_) {
    Complete(id, method, Failure("", 409, "已经入房"));
    return;
  }
  data["media"] = {{"audio", {{"send", true}, {"muted", true}}},
                   {"video", {{"send", false}}}};
  data["policy"] = {
      {"talk_mode", config_.value("talk_mode", std::string("free"))},
      {"media_mode", config_.value("media_mode", std::string("auto"))}};
  TALK_LOG_INFO("session", "加入房间 room={} mode={}",
                data.value("room_id", std::string()),
                data["policy"].value("media_mode", std::string("auto")));
  joining_ = true;
  const uint64_t epoch = epoch_;
  const Json intent = data;
  signaling_.Request(
      method, data, [this, id, method, intent, epoch](Json result) {
        joining_ = false;
        if (epoch != epoch_) {
          Complete(id, method, Failure("", 487, "入房已取消"));
          return;
        }
        if (result.value("ok", false)) {
          join_intent_ = intent;
          room_id_ = intent.at("room_id").get<std::string>();
          const Json view = result.value("data", Json::object());
          TALK_LOG_INFO("session", "已入房 room={} mode={} peers={}",
                        room_id_,
                        view.value("media_mode", std::string("sfu")),
                        view.value("peers", Json::array()).size());
          media_.ApplyModeView(view);
          const Json room = view.value("room", Json::object());
          ApplyTalkMode(room.value("policy", Json::object()));
          if (talk_mode_ != "free") {
            // PTT/审核模式入房即静音，等 floor 授予才放行本地采集。
            floor_open_ = false;
            media_.gate()->store(true);
            media_.SetMuted(true);
          }
          events_.Emit("joined", view);
          ApplyFloor(view.value("floor", Json::object()));
          // 每次入房都从「未共享」开始，界面按钮文案以此为准。
          video_sharing_ = false;
          camera_sharing_ = false;
          ++video_epoch_;
          ++camera_epoch_;
          events_.Emit("video.state", {{"sharing", false}, {"source", "screen"}});
          events_.Emit("video.state", {{"sharing", false}, {"source", "camera"}});
          if (media_.enabled()) {
            if (media_.mode() == MediaController::Mode::kP2p) {
              media_.StartP2p();
            } else {
              media_.Publish();
            }
          }
        }
        Complete(id, method, result);
      });
}

void CallSession::HandleLeave(uint64_t id, const std::string& method) {
  if (room_id_.empty()) {
    Complete(id, method, Failure("", 409, "未入房"));
    return;
  }
  TALK_LOG_INFO("session", "离开房间 room={}", room_id_);
  join_intent_ = Json::object();
  ResetMedia();
  signaling_.Request(
      method, {{"room_id", room_id_}}, [this, id, method](Json result) {
        if (result.value("ok", false)) {
          room_id_.clear();
          events_.Emit("left", Json::object());
        }
        Complete(id, method, result);
      });
}

void CallSession::HandleSetMute(uint64_t id, const std::string& method,
                                Json data) {
  if (room_id_.empty()) {
    Complete(id, method, Failure("", 409, "未入房"));
    return;
  }
  const bool muted = data.at("muted").get<bool>();
  if (!muted && talk_mode_ != "free") {
    // PTT/审核模式下开麦必须先拿发言权，服务端也会回 403；本地直接拦住。
    Complete(id, method, Failure("", 403, "需先申请发言权"));
    return;
  }
  const uint64_t generation = ++mute_epoch_;
  TALK_LOG_INFO("session", "设置静音 muted={} generation={}", muted,
                generation);
  if (muted) {
    media_.gate()->store(true);
    media_.SetMuted(true);
  }
  signaling_.Request(
      method, {{"source", "mic"}, {"muted", muted}},
      [this, id, method, muted, generation](Json result) {
        if (generation != mute_epoch_) {
          Complete(id, method, Failure("", 487, "静音命令已被更新"));
          return;
        }
        // 服务端拒绝或超时时保持静音，避免误开麦。
        const bool effective = muted || !result.value("ok", false);
        media_.SetMuted(
            effective, [this, id, method, generation, effective,
                        result](MediaResult applied) mutable {
              if (generation != mute_epoch_) {
                Complete(id, method,
                         Failure("", 487, "静音命令已被更新"));
                return;
              }
              if (!applied.ok) {
                result = Failure("", applied.code, applied.error);
              }
              const bool is_muted = effective || !result.value("ok", false);
              media_.gate()->store(is_muted);
              events_.Emit("muted", {{"muted", is_muted}});
              Complete(id, method, result);
            });
      });
}

void CallSession::HandleFloorCommand(uint64_t id, const std::string& method,
                                     Json data) {
  if (room_id_.empty()) {
    Complete(id, method, Failure("", 409, "未入房"));
    return;
  }
  TALK_LOG_INFO("session", "S4 请求 method={}", method);
  signaling_.Request(method, data, [this, id, method](Json result) {
    if (result.value("ok", false)) {
      const Json payload = result.value("data", Json::object());
      if (payload.contains("policy") && payload["policy"].is_object()) {
        ApplyTalkMode(payload["policy"]);
      }
      if (payload.contains("floor")) {
        ApplyFloor(payload["floor"]);
      }
    } else {
      const Json error = result.value("error", Json::object());
      TALK_LOG_WARN("session", "S4 请求失败 method={} code={} msg={}", method,
                    error.value("code", 0), error.value("msg", std::string()));
    }
    Complete(id, method, result);
  });
}

void CallSession::ApplyTalkMode(const Json& policy) {
  const std::string mode = policy.value("talk_mode", std::string("free"));
  const bool changed = mode != talk_mode_;
  ptt_queue_ = policy.value("ptt_queue", true);
  talk_mode_ = mode;
  if (talk_mode_ == "free") {
    // free 模式由 media.setMute 直接控制采集，发言权不再约束本地门。
    floor_open_ = false;
    floor_ = Json::object();
    return;
  }
  if (changed) {
    TALK_LOG_INFO("session", "对讲模式 {} ptt_queue={}", mode, ptt_queue_);
    // 切进 PTT/审核时服务端会收回发言权并暂停 producer，本地也要闭嘴。
    if (floor_open_) {
      SetFloorMic(false);
    }
  }
}

void CallSession::ApplyFloor(Json view) {
  floor_ = view.is_object() ? std::move(view) : Json::object();
  const bool mine = !self_uid_.empty() && HolderOf(floor_) == self_uid_;
  if (talk_mode_ != "free") {
    SetFloorMic(mine);
  }
  Json out = floor_;
  if (out.contains("expires_at") && out["expires_at"].is_number()) {
    const int64_t expires = out["expires_at"].get<int64_t>();
    // expires_at 是服务端 epoch ms，按时钟偏差换算成本地剩余时间。
    out["expires_in_ms"] =
        std::max<int64_t>(0, expires - (NowMs() - server_offset_ms_));
  }
  out["self"] = mine;
  out["talk_mode"] = talk_mode_;
  out["ptt_queue"] = ptt_queue_;
  events_.Emit("floor.state", std::move(out));
}

void CallSession::SetFloorMic(bool open) {
  if (floor_open_ == open) {
    return;
  }
  floor_open_ = open;
  // 服务端授予发言权时已 resume producer，这里只放开本地上行，
  // 不能再发 media.setMute，否则会把服务端的授予结果覆盖掉。
  media_.gate()->store(!open);
  media_.SetMuted(!open);
  TALK_LOG_INFO("session", "本地发言门 open={} talk_mode={}", open,
                talk_mode_);
  events_.Emit("floor.local", {{"open", open}});
}

int64_t CallSession::NowMs() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

void CallSession::HandleVideoStart(uint64_t id, const std::string& method,
                                   Json data) {
  const std::string source = data.value("source", std::string("screen"));
  const bool is_camera = source == "camera";
  if (!is_camera && source != "screen") {
    Complete(id, method, Failure("", 400, "未知视频源：" + source));
    return;
  }
  if (room_id_.empty()) {
    Complete(id, method, Failure("", 409, "未入房"));
    return;
  }
  if (!media_.enabled()) {
    Complete(id, method, Failure("", 409, "媒体未启用"));
    return;
  }
  uint64_t& epoch = is_camera ? camera_epoch_ : video_epoch_;
  const uint64_t generation = ++epoch;
  TALK_LOG_INFO("session", "启动视频 source={} generation={}", source,
                generation);
  media_.StartVideo(source, [this, id, method, source, is_camera,
                             generation](MediaResult started) {
    if (generation != (is_camera ? camera_epoch_ : video_epoch_)) {
      Complete(id, method, Failure("", 487, "视频命令已被更新"));
      return;
    }
    if (!started.ok) {
      Complete(id, method, Failure("", started.code, started.error));
      return;
    }
    auto finish = [this, id, method, source, is_camera,
                   started](Json result) {
      if (!result.value("ok", false)) {
        // 服务端未恢复 producer：回滚本地采集，避免只采不发。
        TALK_LOG_WARN("session", "服务端未开启视频 source={}，回滚本地采集",
                      source);
        media_.StopVideo(source);
        Complete(id, method, result);
        return;
      }
      if (is_camera) {
        camera_sharing_ = true;
      } else {
        video_sharing_ = true;
      }
      events_.Emit("video.state", {{"sharing", true}, {"source", source}});
      Complete(id, method, Response("", started.data));
    };
    if (media_.mode() == MediaController::Mode::kP2p) {
      // P2P 没有服务端 producer，sender 激活即生效。
      finish({{"ok", true}});
      return;
    }
    // 摄像头/屏幕各自的 producer 都初始 paused，必须用 source 精确恢复，
    // 否则服务端不会向对端发流（两路互不牵连）。
    signaling_.Request(
        "media.setMute",
        {{"source", is_camera ? "camera" : "screen"}, {"muted", false}},
        [finish = std::move(finish)](Json result) mutable {
          finish(std::move(result));
        });
  });
}

void CallSession::HandleVideoStop(uint64_t id, const std::string& method,
                                  Json data) {
  const std::string source = data.value("source", std::string("screen"));
  const bool is_camera = source == "camera";
  if (!is_camera && source != "screen") {
    Complete(id, method, Failure("", 400, "未知视频源：" + source));
    return;
  }
  if (room_id_.empty()) {
    Complete(id, method, Failure("", 409, "未入房"));
    return;
  }
  uint64_t& epoch = is_camera ? camera_epoch_ : video_epoch_;
  const uint64_t generation = ++epoch;
  TALK_LOG_INFO("session", "停止视频 source={} generation={}", source,
                generation);
  media_.StopVideo(source, [this, id, method, source, is_camera,
                            generation](MediaResult stopped) {
    if (generation != (is_camera ? camera_epoch_ : video_epoch_)) {
      Complete(id, method, Failure("", 487, "视频命令已被更新"));
      return;
    }
    if (is_camera) {
      camera_sharing_ = false;
    } else {
      video_sharing_ = false;
    }
    if (!stopped.ok) {
      events_.Emit("video.state", {{"sharing", false}, {"source", source}});
      Complete(id, method, Failure("", stopped.code, stopped.error));
      return;
    }
    events_.Emit("video.state", {{"sharing", false}, {"source", source}});
    if (media_.mode() == MediaController::Mode::kP2p) {
      // P2P 不经过服务端 producer，本地 sender 已关，直接返回。
      Complete(id, method, Response("", Json::object()));
      return;
    }
    // 两路各自用 source 精确暂停：停共享会释放 max_publishers 名额，并通过
    // media.producer 广播让对端立即收画面（不必等 5 秒无帧超时）。
    const std::string target = is_camera ? "camera" : "screen";
    signaling_.Request(
        "media.setMute", {{"source", target}, {"muted", true}},
        [this, id, method, target](Json result) {
          if (!result.value("ok", false)) {
            TALK_LOG_WARN("session", "服务端暂停视频失败 source={}，对端可能停在"
                                    "最后一帧",
                          target);
          }
          Complete(id, method, Response("", Json::object()));
        });
  });
}

void CallSession::HandleSessionRequest(uint64_t id, const std::string& method,
                                       Json data) {
  signaling_.Request(method, data, [this, id, method](Json result) {
    if (method == "session.refreshIce" && result.value("ok", false)) {
      ice_servers_ = result.value("data", Json::object())
                        .value("ice_servers", Json::array());
      media_.SetIceServers(ice_servers_);
      result["data"] = Json::object();
    }
    Complete(id, method, result);
  });
}

void CallSession::Complete(uint64_t id, const std::string& method,
                           Json result) {
  if (stopping_) {
    return;
  }
  if (!result.value("ok", true)) {
    const Json error = result.value("error", Json::object());
    TALK_LOG_WARN("session", "命令失败 id={} method={} code={} msg={}",
                  id, method, error.value("code", 500),
                  error.value("msg", std::string()));
  }
  result.erase("v");
  result.erase("type");
  result["id"] = id;
  result["method"] = method;
  if (result.contains("error")) {
    result["error"] = {{"code", result["error"].value("code", 500)},
                       {"msg", "请求未成功"}};
  }
  events_.Emit("result", result);
}

void CallSession::Shutdown() {
  TALK_LOG_INFO("session", "会话关闭 room={} session={}", room_id_,
                session_id_);
  stopping_ = true;
  retry_ = false;
  ++epoch_;
  media_.gate()->store(true);
  heartbeat_.cancel();
  reconnect_.cancel();
  signaling_.Dispose();
}

void CallSession::StartConnect(uint64_t operation, bool recovery) {
  reconnect_.cancel();
  TALK_LOG_INFO("session", "建立连接 recovery={} session={}", recovery,
                session_id_);
  if (recovery && std::chrono::steady_clock::now() > resume_deadline_) {
    TALK_LOG_WARN("session", "恢复窗口已过期，重置会话与媒体");
    session_id_.clear();
    ResetMedia();
    room_id_.clear();
  }
  const uint64_t epoch = ++epoch_;
  ready_ = false;

  SignalingClient::Handlers handlers;
  handlers.on_open = [this, epoch, operation, recovery] {
    HandleOpen(epoch, operation, recovery);
  };
  handlers.on_message = [this, epoch](Json message) {
    if (epoch != epoch_) {
      return;
    }
    HandleMessage(std::move(message));
  };
  handlers.on_closed = [this, epoch, operation](
                           const SignalingClient::CloseInfo& info) {
    if (epoch != epoch_) {
      return;
    }
    HandleClosed(info, operation);
  };
  handlers.on_error = [this](int code, std::string message) {
    events_.Emit("error", {{"code", code}, {"msg", std::move(message)}});
  };

  if (!signaling_.Connect(url_, token_, std::move(handlers))) {
    retry_ = false;
    TALK_LOG_ERROR("session", "连接失败：地址无效 url={}", url_);
    Complete(operation, "connect", Failure("", 400, "地址无效"));
    return;
  }
  events_.Emit("state",
               {{"state", recovery ? "reconnecting" : "connecting"}});
}

void CallSession::HandleOpen(uint64_t epoch, uint64_t operation,
                             bool recovery) {
  Json hello = {
      {"client",
       {{"platform", "windows-x64"},
        {"version", talk_version()},
        {"sdk", "webrtc-m146"}}},
      {"caps",
       {{"audio", {{"codecs", Json::array({"opus"})}, {"aec", "webrtc"}}},
        {"p2p", true},
        // P2P 已预发布两路视频 m-line，服务端据此在 room.mediaMode
        // 里透出 p2p_video=true，供其它端判断直连能否承载视频。
        {"p2p_video", true},
        {"sfu_sdp", "unified-plan"}}}};
  if (!session_id_.empty()) {
    hello["resume"] = {{"session_id", session_id_}};
  }
  signaling_.Request(
      "session.hello", hello,
      [this, epoch, operation, recovery](Json result) {
        if (epoch != epoch_) {
          Complete(operation, "connect", Failure("", 487, "连接已取消"));
          return;
        }
        if (!result.value("ok", false)) {
          const Json error = result.value("error", Json::object());
          TALK_LOG_WARN("session", "hello 失败 code={} msg={}",
                        error.value("code", 500),
                        error.value("msg", std::string()));
          Complete(operation, "connect", result);
          HandleLost();
          return;
        }
        const Json data = result.value("data", Json::object());
        if (!data.contains("session_id") || !data["session_id"].is_string()) {
          TALK_LOG_WARN("session", "hello 响应缺少 session_id");
          HandleLost();
          return;
        }
        session_id_ = data["session_id"].get<std::string>();
        self_uid_ = data.value("uid", std::string());
        // hello 的 server_time 是唯一的对时基准，心跳会持续校正。
        const int64_t server_time =
            data.value("server_time", static_cast<int64_t>(0));
        if (server_time > 0) {
          server_offset_ms_ = NowMs() - server_time;
        }
        ice_servers_ = data.value("ice_servers", Json::array());
        media_.SetIceServers(ice_servers_);
        ready_ = true;
        retry_count_ = 0;
        const bool resumed = data.value("resumed", false);
        heartbeat_seconds_ = Clamp(data.value("heartbeat_interval", 15), 1, 30);
        resume_seconds_ = Clamp(data.value("resume_ttl", 60), 1, 3600);
        TALK_LOG_INFO(
            "session",
            "hello 完成 session={} resumed={} heartbeat={}s resume_ttl={}s "
            "ice={}",
            session_id_, resumed, heartbeat_seconds_, resume_seconds_,
            ice_servers_.size());
        if (!resumed) {
          ResetMedia();
          room_id_.clear();
        }
        Json public_data = data;
        public_data.erase("ice_servers");
        public_data["recovering"] = recovery;
        Complete(operation, "connect", Response("", public_data));
        events_.Emit("ready", public_data);
        SchedulePing();
        if (recovery && resumed && data.contains("room")) {
          const Json view = data.value("room", Json::object());
          if (!view.is_object()) {
            return;
          }
          room_id_ =
              view.value("room", Json::object()).value("room_id",
                                                       std::string());
          TALK_LOG_INFO("session", "恢复入房 room={}", room_id_);
          const std::string previous = media_.p2p_session();
          media_.ApplyModeView(view);
          ApplyTalkMode(view.value("room", Json::object())
                            .value("policy", Json::object()));
          events_.Emit("joined", view);
          ApplyFloor(view.value("floor", Json::object()));
          // 断线期间采集仍在跑，按实际状态回报，避免按钮与画面不一致。
          events_.Emit("video.state",
                       {{"sharing", video_sharing_}, {"source", "screen"}});
          events_.Emit("video.state",
                       {{"sharing", camera_sharing_}, {"source", "camera"}});
          if (media_.enabled()) {
            if (media_.mode() == MediaController::Mode::kP2p &&
                (!media_.p2p_started() ||
                 previous != media_.p2p_session())) {
              media_.StartP2p();
            }
            if (media_.mode() == MediaController::Mode::kSfu &&
                media_.p2p_started()) {
              media_.CloseP2p();
              media_.Publish();
            }
          }
          Command(0, "media.setMute", {{"source", "mic"}, {"muted", true}});
        } else if (recovery && !join_intent_.empty()) {
          Command(0, "room.join", join_intent_);
        }
      });
}

void CallSession::HandleClosed(const SignalingClient::CloseInfo& info,
                               uint64_t operation) {
  TALK_LOG_INFO("session", "连接关闭 handshake={} status={} code={} fatal={}",
                info.handshake_failed, info.status, info.code, info.fatal);
  if (info.fatal) {
    retry_ = false;
  }
  if (info.handshake_failed) {
    Complete(operation, "connect",
             Failure("", info.status != 0 ? info.status : 480, "握手失败"));
  }
  HandleLost();
}

void CallSession::HandleMessage(Json message) {
  const std::string type = message["type"].get<std::string>();
  const std::string method = message["method"].get<std::string>();
  Json data = message.value("data", Json::object());
  if (type == "req") {
    const std::string id = message["id"].get<std::string>();
    TALK_LOG_INFO("session", "服务端请求 method={} id={}", method, id);
    HandleServerRequest(id, method, std::move(data));
    return;
  }
  if (method != "p2p.candidate" && method != "media.state") {
    TALK_LOG_DEBUG("session", "收到通知 method={}", method);
  }
  HandleNotification(method, std::move(data));
}

void CallSession::HandleServerRequest(const std::string& id,
                                      const std::string& method, Json data) {
  if (method == "room.mediaMode") {
    HandleMediaModeRequest(id, std::move(data));
    return;
  }
  if (method == "sfu.offer" && !media_.enabled()) {
    TALK_LOG_WARN("session",
                  "收到 sfu.offer 但媒体未启用：audio_backend=none 时无法"
                  "订阅音频，请检查配置并重启客户端");
    signaling_.Send(Failure(id, 501, "媒体未启用，无法订阅音频"));
    return;
  }
  if (method != "sfu.offer") {
    TALK_LOG_WARN("session", "不支持的服务端请求 method={} id={}", method,
                  id);
    signaling_.Send(Failure(id, 501, "该客户端尚不支持此请求"));
    return;
  }
  HandleSfuOffer(id, data);
}

void CallSession::HandleMediaModeRequest(const std::string& id, Json data) {
  const std::string mode =
      data.value("mode", data.value("media_mode", std::string()));
  // p2p_video 是服务端根据双方 caps 算出的能力位（双方都声明才是 true），
  // 客户端已预发布两路视频 m-line，这里只记录便于核对协商结果。
  TALK_LOG_INFO("session", "切换媒体模式 mode={} p2p_video={}", mode,
                data.value("p2p_video", false));
  if (!media_.enabled()) {
    const std::string applied = mode == "p2p" ? "p2p" : "sfu";
    media_.ApplyModeView({{"media_mode", applied}});
    EmitModeChanged(applied);
    signaling_.Send(Response(id, Json::object()));
    return;
  }
  if (mode == "p2p") {
    if (!data.contains("p2p") || !data["p2p"].is_object()) {
      TALK_LOG_WARN("session", "切换 P2P 失败：缺少分配");
      signaling_.Send(Failure(id, 400, "缺少 P2P 分配"));
      return;
    }
    media_.SwitchToP2p(data["p2p"]);
    EmitModeChanged("p2p");
    signaling_.Send(Response(id, Json::object()));
    return;
  }
  if (mode == "sfu") {
    // 先通知界面：共享屏幕等只支持 SFU 的能力要立刻跟着放开，
    // 不必等发布与订阅全部完成。
    EmitModeChanged("sfu");
    media_.SwitchToSfu([this, id] {
      signaling_.Send(Response(id, Json::object()));
      // P2P 期间两路 producer 都不走 setMute（P2P 没有 producer）：凡是在 P2P
      // 里停过的采集，切回 SFU 重新发布后 producer 仍是 paused，必须按 source
      // 逐路显式恢复，否则对端只能看到最后一张画面。
      const std::string sources[] = {"camera", "screen"};
      for (const std::string& source : sources) {
        const bool sharing = source == "camera" ? camera_sharing_
                                                : video_sharing_;
        if (!sharing) {
          continue;
        }
        signaling_.Request(
            "media.setMute", {{"source", source}, {"muted", false}},
            [source](Json result) {
              if (!result.value("ok", false)) {
                TALK_LOG_WARN("session",
                              "切回 SFU 后恢复视频 producer 失败 source={}",
                              source);
              }
            });
      }
    });
    return;
  }
  TALK_LOG_WARN("session", "切换媒体模式失败：mode={} 无效", mode);
  signaling_.Send(Failure(id, 400, "模式无效"));
}

void CallSession::EmitModeChanged(const std::string& mode) {
  TALK_LOG_INFO("session", "媒体模式变更 mode={}", mode);
  events_.Emit("mode.changed", {{"mode", mode}});
}

void CallSession::HandleSfuOffer(const std::string& id, const Json& data) {
  if (!data.is_object() || !data.contains("sdp") ||
      !data["sdp"].is_string()) {
    TALK_LOG_WARN("session", "SFU offer 缺少 SDP id={}", id);
    signaling_.Send(Failure(id, 400, "缺少 SDP"));
    return;
  }
  if (room_id_.empty() && !joining_) {
    TALK_LOG_WARN("session", "SFU offer 到达时未入房 id={}", id);
    signaling_.Send(Failure(id, 409, "未入房"));
    return;
  }
  const uint64_t epoch = epoch_;
  const std::string sdp = data["sdp"].get<std::string>();
  const Json consumers = data.value("consumers", Json::array());
  TALK_LOG_INFO("session", "应用 SFU offer id={} sdp={} consumers={}", id,
                sdp.size(), consumers.size());
  media_.ApplySfuOffer(sdp, consumers, [this, id, epoch](MediaResult result) {
    if (epoch != epoch_) {
      return;
    }
    if (result.ok) {
      TALK_LOG_INFO("session", "SFU 订阅完成 id={}", id);
      signaling_.Send(Response(id, result.data));
      media_.FinishModeSwitch();
      events_.Emit("subscribed",
                   {{"ok", true}, {"consumers", media_.consumers()}});
      return;
    }
    TALK_LOG_ERROR("session", "SFU 订阅失败 id={} code={} error={}", id,
                   result.code, result.error);
    signaling_.Send(Failure(id, result.code, result.error));
    events_.Emit("subscribed",
                 {{"ok", false}, {"consumers", media_.consumers()}});
  });
}

void CallSession::HandleNotification(const std::string& method, Json data) {
  if (method == "p2p.offer") {
    const std::string session = data.value("session", std::string());
    const std::string from = data.value("from", std::string());
    const std::string sdp = data.value("sdp", std::string());
    TALK_LOG_INFO("session", "收到 P2P offer session={} from={} sdp={}",
                  session, from, sdp.size());
    if (session.empty() || sdp.empty()) {
      return;
    }
    if (media_.p2p_session().empty()) {
      media_.AdoptP2pSession(session, from);
    }
    if (session != media_.p2p_session()) {
      return;
    }
    media_.AnswerP2p(session, from, sdp);
    return;
  }
  if (method == "p2p.answer") {
    TALK_LOG_INFO("session", "收到 P2P answer session={}",
                  data.value("session", std::string()));
    media_.AcceptP2pAnswer(data.value("session", std::string()),
                           data.value("sdp", std::string()));
    return;
  }
  if (method == "p2p.candidate") {
    TALK_LOG_DEBUG("session", "收到 P2P candidate session={}",
                   data.value("session", std::string()));
    media_.AddP2pCandidate(data.value("session", std::string()),
                           data.value("candidate", std::string()),
                           data.value("sdp_mid", std::string("0")),
                           data.value("sdp_mline_index", 0));
    return;
  }
  if (method == "p2p.close") {
    TALK_LOG_INFO("session", "收到 P2P close");
    media_.HandleP2pClose(data);
    return;
  }
  if (method == "room.peerJoined" || method == "room.peerUpdated" ||
      method == "room.peerLeft") {
    TALK_LOG_INFO("session", "房间成员变化 method={} uid={} audio={}", method,
                  data.value("uid", std::string()),
                  data.contains("audio") ? data["audio"].dump()
                                         : std::string("-"));
    events_.Emit(method, std::move(data));
    return;
  }
  if (method == "session.kicked") {
    // kicked：被主持人移出；replaced：同一 uid 在别处重新登录。两者都不重连。
    TALK_LOG_WARN("session", "会话被服务端关闭 reason={}",
                  data.value("reason", std::string("replaced")));
    retry_ = false;
    events_.Emit(method, data);
    HandleLost();
    return;
  }
  if (method == "floor.state") {
    ApplyFloor(std::move(data));
    return;
  }
  if (method == "room.policyChanged") {
    ApplyTalkMode(data);
    events_.Emit(method, std::move(data));
    return;
  }
  if (method == "floor.revoked") {
    // 服务端已收回发言权并暂停 producer，本地采集门与界面要立刻跟上。
    TALK_LOG_WARN("session", "发言权被收回 reason={}",
                  data.value("reason", std::string()));
    floor_open_ = false;
    media_.gate()->store(true);
    ++mute_epoch_;
    media_.SetMuted(true);
    events_.Emit(method, std::move(data));
    return;
  }
  if (method == "room.closed") {
    TALK_LOG_INFO("session", "房间已关闭 room={}", room_id_);
    room_id_.clear();
    join_intent_ = Json::object();
    ResetMedia();
  }
  if (method == "media.forceMute") {
    TALK_LOG_WARN("session", "服务端强制静音 method={}", method);
    media_.gate()->store(true);
    ++mute_epoch_;
    media_.SetMuted(true);
  }
  events_.Emit(method, data);
}

void CallSession::HandleLost() {
  if (stopping_) {
    return;
  }
  TALK_LOG_WARN("session", "连接丢失 ready={} retry={} room={}", ready_,
                retry_, room_id_);
  if (ready_) {
    resume_deadline_ = std::chrono::steady_clock::now() +
                       std::chrono::seconds(resume_seconds_);
  }
  media_.gate()->store(true);
  ++epoch_;
  ++mute_epoch_;
  ready_ = false;
  joining_ = false;
  disconnecting_ = false;
  heartbeat_.cancel();
  reconnect_.cancel();
  signaling_.Abort(480, "连接断开");
  media_.Invalidate();
  media_.SetMuted(true);
  events_.Emit("state",
               {{"state", retry_ ? "reconnecting" : "disconnected"}});
  if (!retry_) {
    session_id_.clear();
    room_id_.clear();
    join_intent_ = Json::object();
    ResetMedia();
    return;
  }
  const int seconds = std::min(15, 1 << std::min(retry_count_++, 4));
  TALK_LOG_INFO("session", "计划重连 delay={}ms attempt={}",
                seconds * 1000, retry_count_);
  reconnect_.expires_after(std::chrono::milliseconds(
      seconds * 1000 + static_cast<int>(random_() % 250)));
  reconnect_.async_wait([this](const asio::error_code& error) {
    if (!error && retry_) {
      StartConnect(0, true);
    }
  });
}

void CallSession::SchedulePing() {
  TALK_LOG_DEBUG("session", "安排心跳 interval={}s", heartbeat_seconds_);
  heartbeat_.expires_after(std::chrono::seconds(heartbeat_seconds_));
  heartbeat_.async_wait([this](const asio::error_code& error) {
    if (error || !ready_) {
      return;
    }
    const uint64_t epoch = epoch_;
    signaling_.Request("session.ping", Json::object(),
                       [this, epoch](Json result) {
                         if (epoch != epoch_) {
                           return;
                         }
                         if (!result.value("ok", false)) {
                           HandleLost();
                         } else {
                           const Json payload =
                               result.value("data", Json::object());
                           const int64_t server_time = payload.value(
                               "server_time", static_cast<int64_t>(0));
                           if (server_time > 0) {
                             server_offset_ms_ = NowMs() - server_time;
                           }
                           SchedulePing();
                         }
                       });
  });
}

void CallSession::ResetMedia() {
  TALK_LOG_DEBUG("session", "重置媒体状态");
  ++mute_epoch_;
  // 丢弃在途的视频命令结果；媒体实例销毁时采集线程会随之退出。
  ++video_epoch_;
  ++camera_epoch_;
  video_sharing_ = false;
  camera_sharing_ = false;
  floor_open_ = false;
  floor_ = Json::object();
  media_.Reset();
}

}  // namespace talk
