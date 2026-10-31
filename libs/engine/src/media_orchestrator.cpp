#include "rtcwin/engine/media_orchestrator.h"

#include "engine_log.h"
#include "rtcwin/camera_win/camera_capture.h"
#include "rtcwin/signaling/protocol.h"

#include <algorithm>
#include <chrono>
#include <exception>
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

struct MediaOrchestrator::Impl {
  explicit Impl(Context context)
      : context(std::move(context)),
        media_mode_timer(context.signaling.context()) {}

  ~Impl() { Reset(); }

  void Configure(const webrtc_media::MediaConfig& value,
                 std::string uid) {
    std::lock_guard<std::mutex> lock(config_mutex);
    config = value;
    local_uid = std::move(uid);
  }

  void SetIceServers(std::vector<webrtc_media::IceServer> ice_servers) {
    {
      std::lock_guard<std::mutex> lock(config_mutex);
      config.ice_servers = ice_servers;
    }
    if (ice_servers.empty()) return;
    context.post_to_worker([this, ice_servers = std::move(ice_servers)] {
      if (!media_session) return;
      webrtc_media::MediaError error;
      if (!media_session->SetIceServers(ice_servers, &error)) {
        LOG_WARN("update ICE servers failed code={} message={}", error.code,
                 error.message);
      }
    });
  }

  bool audio_requested() const {
    std::lock_guard<std::mutex> lock(config_mutex);
    return config.audio_backend != "none";
  }

  bool Initialize(const Json& join_data, std::uint64_t generation,
                  webrtc_media::MediaError* error) {
    if (!context.session.IsCurrent(generation)) {
      if (error) *error = {409, "会话已失效"};
      return false;
    }
    Reset();

    SessionMediaMode mode = context.session.media_mode();
    if (mode == SessionMediaMode::Unknown) {
      mode = SessionMediaMode::Sfu;
      if (!context.session.SetMediaMode(generation, mode)) {
        if (error) *error = {409, "会话已失效"};
        return false;
      }
    }

    webrtc_media::MediaConfig media_config;
    {
      std::lock_guard<std::mutex> lock(config_mutex);
      media_config = config;
    }
    camera_device_index = media_config.camera_device_index;
    const Json room = join_data.value("room", Json::object());
    const Json policy = room.value("policy", Json::object());
    const Json video = policy.value("video", Json::object());
    media_config.camera_publish_enabled = video.value("enabled", true);
    media_config.screen_publish_enabled = video.value("enabled", true);

    if (mode == SessionMediaMode::P2p && join_data.contains("p2p") &&
        join_data["p2p"].is_object()) {
      const Json& p2p = join_data["p2p"];
      p2p_session = p2p.value("session", std::string());
      p2p_peer = p2p.value("peer", std::string());
      p2p_offerer = p2p.value("role", std::string()) == "offerer";
      p2p_video_allowed = join_data.value("p2p_video", false);
    }

    media_session = std::make_unique<webrtc_media::MediaSession>();
    webrtc_media::MediaError media_error;
    const bool ok = media_session->Initialize(
        media_config,
        [this, generation](std::string method, std::string data) {
          context.post_to_worker(
              [this, generation, method = std::move(method),
               data = std::move(data)] {
                if (!context.session.IsCurrent(generation)) return;
                try {
                  HandleMediaEvent(method, Json::parse(data), generation);
                } catch (const std::exception& error) {
                  LOG_WARN("discard invalid media event method={} error={}",
                           method, error.what());
                }
              });
        },
        [this, generation](webrtc_media::RemoteVideoFrame frame) {
          OnRemoteVideoFrame(frame, generation);
        },
        &media_error);
    if (!ok) {
      if (error) *error = media_error;
      media_session.reset();
      return false;
    }
    media_session->SetLocalScreenFrameCallback(
        [this, generation](const webrtc_media::VideoFrameData& frame) {
          OnLocalScreenFrame(frame, generation);
        });
    if (!p2p_peer.empty()) media_session->SetP2pPeerUid(p2p_peer);
    if (!context.session.IsCurrent(generation)) {
      media_session->Shutdown();
      media_session.reset();
      if (error) *error = {409, "会话已失效"};
      return false;
    }

    const auto status = media_session->status();
    context.events.EmitNotification(
        "media.ready",
        Json{{"recording_device", status.recording_device},
             {"playout_device", status.playout_device}});
    LOG_INFO("media ready source={} recording_device={} playout_device={}",
             media_config.audio_source, status.recording_device,
             status.playout_device);
    return true;
  }

  void Start(std::uint64_t generation) {
    if (!context.session.IsCurrent(generation) ||
        !context.session.media_ready()) {
      return;
    }
    if (context.session.media_mode() == SessionMediaMode::P2p) {
      StartP2p(generation);
    } else {
      PublishSfu(generation);
    }
  }

  void Reset() {
    camera_generation.fetch_add(1);
    screen_generation.fetch_add(1);
    mute_generation.fetch_add(1);
    camera_publishing.store(false);
    camera_preview_active = false;
    camera_toggle_pending = false;
    screen_toggle_pending = false;
    StopCameraCaptureIfIdle();
    if (media_session) {
      media_session->Shutdown();
      media_session.reset();
    }
    p2p_session.clear();
    p2p_peer.clear();
    p2p_offerer = false;
    p2p_video_allowed = false;
    sfu_paused = false;
    sfu_publish_ready = false;
    sfu_subscribe_ready = false;
    pending_sfu_request_id.clear();
    pending_sfu_room_mode = Json::object();
    context.signaling.Post([this] { media_mode_timer.cancel(); });
  }

  void HandleCameraDevices(const EngineCommand& command) {
    const auto devices = camera_win::CameraService::Enumerate();
    Json cameras = Json::array();
    for (const auto& device : devices) {
      cameras.push_back(
          {{"id", std::to_string(device.index)}, {"name", device.name}});
    }
    context.events.EmitResponse(command.id, command.method, true, 0, {},
                                Json{{"cameras", std::move(cameras)}});
  }

  void HandleCameraSelect(const EngineCommand& command) {
    const int next_index =
        command.data.value("device_index", -1);
    if (next_index < 0) {
      context.events.EmitResponse(command.id, command.method, false, 400,
                                  "摄像头设备编号无效");
      return;
    }
    if (camera_toggle_pending) {
      context.events.EmitResponse(command.id, command.method, false, 409,
                                  "摄像头命令正在处理中");
      return;
    }
    const int previous_index = camera_device_index;
    const bool restart =
        camera_preview_active || camera_publishing.load();
    camera.Stop();
    camera_device_index = next_index;
    std::string error;
    if (restart && !EnsureCameraCapture(&error)) {
      camera_device_index = previous_index;
      std::string restore_error;
      EnsureCameraCapture(&restore_error);
      context.events.EmitResponse(
          command.id, command.method, false, 1501,
          error.empty() ? "切换摄像头失败" : error);
      return;
    }
    Json data = {{"device_index", camera_device_index},
                 {"device", camera.device_name()},
                 {"width", camera.width()},
                 {"height", camera.height()},
                 {"fps", camera.fps()},
                 {"restarted", restart}};
    context.events.EmitResponse(command.id, command.method, true, 0, {},
                                data);
    if (restart && camera_preview_active) {
      context.events.EmitNotification("camera.preview", data);
    }
  }

  void HandleCameraPreviewStart(const EngineCommand& command) {
    camera.Stop();
    camera_preview_active = false;
    camera_device_index = command.data.value("device_index", -1);
    std::string error;
    if (!EnsureCameraCapture(&error)) {
      context.events.EmitResponse(
          command.id, command.method, false, 1501,
          error.empty() ? "camera start failed" : error);
      return;
    }
    camera_preview_active = true;
    Json data = {{"device_index", camera_device_index},
                 {"device", camera.device_name()},
                 {"width", camera.width()},
                 {"height", camera.height()},
                 {"fps", camera.fps()}};
    context.events.EmitResponse(command.id, command.method, true, 0, {}, data);
  }

  void HandleCameraPreviewStop(const EngineCommand& command) {
    camera_preview_active = false;
    StopCameraCaptureIfIdle();
    context.events.EmitResponse(command.id, command.method, true, 0, {},
                                Json::object());
  }

  void HandleSetMute(const EngineCommand& command) {
    const Json& request = command.data;
    if (!request.is_object()) {
      context.events.EmitResponse(command.id, command.method, false, 400,
                                  "静音参数无效");
      return;
    }
    if (!request.contains("muted") || !request["muted"].is_boolean()) {
      context.events.EmitResponse(command.id, command.method, false, 400,
                                  "缺少 muted 参数");
      return;
    }
    const EngineSessionSnapshot session = context.session.snapshot();
    const bool joined = session.phase == SessionPhase::Joined &&
                        session.media_enabled;
    if (!joined || !media_session) {
      context.events.EmitResponse(command.id, command.method, false, 409,
                                  "音频媒体未就绪");
      return;
    }
    const bool muted = request["muted"].get<bool>();
    LOG_INFO("media.setMute requested muted={}", muted);
    const std::uint64_t generation = mute_generation.fetch_add(1) + 1;
    webrtc_media::MediaError media_error;
    if (muted && !media_session->SetMuted(true, &media_error)) {
      context.events.EmitResponse(command.id, command.method, false,
                                  media_error.code, media_error.message);
      return;
    }
    const std::uint64_t command_id = command.id;
    const std::uint64_t session_generation = session.generation;
    context.signaling.Request(
        "media.setMute", {{"source", "mic"}, {"muted", muted}},
        [this, command_id, generation, session_generation,
         muted](Json result) {
          context.post_to_worker(
              [this, command_id, generation, session_generation, muted,
               result] {
                if (generation != mute_generation.load()) {
                  context.events.EmitResponse(
                      command_id, "media.setMute", false, 487,
                      "静音命令已被更新");
                  return;
                }
                if (!context.session.IsCurrent(session_generation)) {
                  context.events.EmitResponse(command_id, "media.setMute",
                                              false, 409, "会话已失效");
                  return;
                }
                const bool server_ok = result.value("ok", false);
                const bool effective = muted || !server_ok;
                webrtc_media::MediaError media_error;
                const bool applied =
                    media_session &&
                    media_session->SetMuted(effective, &media_error);
                Json state = {{"muted", applied ? effective : true}};
                LOG_INFO(
                    "media.setMute confirmed requested={} server_ok={} "
                    "effective={} applied={} error={}",
                    muted, server_ok, effective, applied,
                    media_error.message);
                context.events.EmitNotification("media.muted", state);
                if (!server_ok) {
                  context.events.EmitResponse(
                      command_id, "media.setMute", false,
                      ResponseCode(result), ResponseMessage(result));
                } else if (!applied) {
                  context.events.EmitResponse(
                      command_id, "media.setMute", false,
                      media_error.code, media_error.message);
                } else {
                  context.events.EmitResponse(command_id, "media.setMute",
                                              true, 0, {}, state);
                }
              });
        });
  }

  void HandleToggleCamera(const EngineCommand& command) {
    const EngineSessionSnapshot session = context.session.snapshot();
    const bool joined = session.phase == SessionPhase::Joined &&
                        session.media_enabled;
    if (!joined || !media_session) {
      context.events.EmitResponse(command.id, command.method, false, 409,
                                  "媒体尚未就绪");
      return;
    }
    if (context.session.media_mode() == SessionMediaMode::P2p &&
        !p2p_video_allowed) {
      context.events.EmitResponse(command.id, command.method, false, 409,
                                  "P2P 视频未协商，请切换到 SFU");
      return;
    }
    if (camera_toggle_pending) {
      context.events.EmitResponse(command.id, command.method, false, 409,
                                  "摄像头命令正在处理中");
      return;
    }

    const bool enable = !camera_publishing.load();
    if (enable) {
      std::string error;
      if (!EnsureCameraCapture(&error)) {
        context.events.EmitResponse(
            command.id, command.method, false, 1501,
            error.empty() ? "摄像头启动失败" : error);
        return;
      }
    } else {
      camera_publishing.store(false);
      webrtc_media::MediaError media_error;
      media_session->SetCameraEnabled(false, &media_error);
      StopCameraCaptureIfIdle();
    }

    camera_toggle_pending = true;
    const std::uint64_t generation = camera_generation.fetch_add(1) + 1;
    const std::uint64_t command_id = command.id;
    const std::uint64_t session_generation = session.generation;
    LOG_INFO("media.toggleCamera generation={} enable={}", generation, enable);
    context.signaling.Request(
        "media.setMute", Json{{"source", "camera"}, {"muted", !enable}},
        [this, command_id, generation, session_generation,
         enable](Json result) {
          context.post_to_worker(
              [this, command_id, generation, session_generation, enable,
               result] {
                if (generation != camera_generation.load()) {
                  context.events.EmitResponse(
                      command_id, "media.toggleCamera", false, 487,
                      "摄像头命令已被更新");
                  return;
                }
                if (!context.session.IsCurrent(session_generation)) {
                  context.events.EmitResponse(
                      command_id, "media.toggleCamera", false, 409,
                      "会话已失效");
                  return;
                }
                camera_toggle_pending = false;
                const bool server_ok = result.value("ok", false);
                webrtc_media::MediaError media_error;
                bool applied = true;
                if (server_ok && enable) {
                  camera_publishing.store(true);
                  applied =
                      media_session &&
                      media_session->SetCameraEnabled(true, &media_error);
                } else if (!enable) {
                  camera_publishing.store(false);
                  applied =
                      media_session &&
                      media_session->SetCameraEnabled(false, &media_error);
                }
                if (!server_ok || !applied) {
                  camera_publishing.store(false);
                  if (media_session) {
                    media_session->SetCameraEnabled(false, &media_error);
                  }
                  if (!camera_preview_active) camera.Stop();
                }
                const bool effective = server_ok && applied && enable;
                Json state = {{"source", "camera"},
                              {"sharing", effective},
                              {"camera", effective}};
                context.events.EmitNotification("video.state", state);
                if (!server_ok) {
                  context.events.EmitResponse(
                      command_id, "media.toggleCamera", false,
                      ResponseCode(result), ResponseMessage(result));
                } else if (!applied) {
                  context.events.EmitResponse(
                      command_id, "media.toggleCamera", false,
                      media_error.code == 0 ? 1730 : media_error.code,
                      media_error.message.empty()
                          ? "应用摄像头发送状态失败"
                          : media_error.message);
                } else {
                  context.events.EmitResponse(
                      command_id, "media.toggleCamera", true, 0, {}, state);
                }
              });
        });
  }

  void HandleToggleScreenShare(const EngineCommand& command) {
    const EngineSessionSnapshot session = context.session.snapshot();
    const bool joined = session.phase == SessionPhase::Joined &&
                        session.media_enabled;
    if (!joined || !media_session) {
      context.events.EmitResponse(command.id, command.method, false, 409,
                                  "媒体尚未就绪");
      return;
    }
    if (context.session.media_mode() == SessionMediaMode::P2p &&
        !p2p_video_allowed) {
      context.events.EmitResponse(command.id, command.method, false, 409,
                                  "P2P 视频未协商，请切换到 SFU");
      return;
    }
    if (screen_toggle_pending) {
      context.events.EmitResponse(command.id, command.method, false, 409,
                                  "屏幕共享命令正在处理中");
      return;
    }

    const bool enable = !media_session->status().screen_enabled;
    if (!enable) {
      webrtc_media::MediaError stop_error;
      if (!media_session->SetScreenEnabled(false, &stop_error)) {
        context.events.EmitResponse(
            command.id, command.method, false,
            stop_error.code == 0 ? 1734 : stop_error.code,
            stop_error.message.empty() ? "停止屏幕采集失败"
                                       : stop_error.message);
        return;
      }
    }

    screen_toggle_pending = true;
    const std::uint64_t generation = screen_generation.fetch_add(1) + 1;
    const std::uint64_t command_id = command.id;
    const std::uint64_t session_generation = session.generation;
    LOG_INFO("media.toggleScreenShare generation={} enable={}", generation,
             enable);
    context.signaling.Request(
        "media.setMute", Json{{"source", "screen"}, {"muted", !enable}},
        [this, command_id, generation, session_generation,
         enable](Json result) {
          context.post_to_worker(
              [this, command_id, generation, session_generation, enable,
               result] {
                if (generation != screen_generation.load()) {
                  context.events.EmitResponse(
                      command_id, "media.toggleScreenShare", false, 487,
                      "屏幕共享命令已被更新");
                  return;
                }
                if (!context.session.IsCurrent(session_generation)) {
                  context.events.EmitResponse(
                      command_id, "media.toggleScreenShare", false, 409,
                      "会话已失效");
                  return;
                }
                screen_toggle_pending = false;
                const bool server_ok = result.value("ok", false);
                webrtc_media::MediaError media_error;
                bool applied = media_session != nullptr;
                if (applied) {
                  applied =
                      media_session->SetScreenEnabled(enable, &media_error);
                }
                if (!server_ok || !applied) {
                  if (media_session) {
                    media_session->SetScreenEnabled(false, &media_error);
                  }
                }
                const bool effective = server_ok && applied && enable;
                const auto status =
                    media_session ? media_session->status()
                                  : webrtc_media::MediaStatus{};
                Json state = {{"source", "screen"},
                              {"sharing", effective},
                              {"screen", effective},
                              {"camera", status.camera_enabled},
                              {"on", status.camera_enabled || effective}};
                context.events.EmitNotification("video.state", state);
                if (!server_ok) {
                  context.events.EmitResponse(
                      command_id, "media.toggleScreenShare", false,
                      ResponseCode(result), ResponseMessage(result));
                } else if (!applied) {
                  context.events.EmitResponse(
                      command_id, "media.toggleScreenShare", false,
                      media_error.code == 0 ? 1734 : media_error.code,
                      media_error.message.empty()
                          ? "应用屏幕共享状态失败"
                          : media_error.message);
                } else {
                  context.events.EmitResponse(
                      command_id, "media.toggleScreenShare", true, 0, {},
                      state);
                }
              });
        });
  }

  void HandleSetVolume(const EngineCommand& command) {
    const Json& request = command.data;
    if (!request.is_object()) {
      context.events.EmitResponse(command.id, command.method, false, 400,
                                  "音量参数无效");
      return;
    }
    double gain = request.value("gain", -1.0);
    if (gain < 0.0 && request.contains("volume")) {
      gain = request.value("volume", 60) / 100.0;
    }
    if (!media_session || !media_session->SetVolume(gain, nullptr)) {
      context.events.EmitResponse(command.id, command.method, false, 400,
                                  "音量必须在 0 到 100 之间");
      return;
    }
    Json data = {{"volume", static_cast<int>(gain * 100.0 + 0.5)}};
    context.events.EmitResponse(command.id, command.method, true, 0, {}, data);
  }

  void HandleSetDeafened(const EngineCommand& command) {
    const Json& request = command.data;
    if (!request.is_object()) {
      context.events.EmitResponse(command.id, command.method, false, 400,
                                  "收听参数无效");
      return;
    }
    const bool value = request.value("deafened", false);
    webrtc_media::MediaError error;
    if (!media_session || !media_session->SetDeafened(value, &error)) {
      context.events.EmitResponse(
          command.id, command.method, false,
          error.code == 0 ? 409 : error.code,
          error.message.empty() ? "音频媒体未就绪" : error.message);
      return;
    }
    deafened = value;
    Json data = {{"deafened", value}};
    context.events.EmitNotification("media.deafened", data);
    context.events.EmitResponse(command.id, command.method, true, 0, {}, data);
  }

  void HandleSelectRecordingDevice(const EngineCommand& command) {
    if (!media_session) {
      context.events.EmitResponse(command.id, command.method, false, 409,
                                  "音频媒体未就绪");
      return;
    }
    const std::string device_id =
        command.data.value("device_id", std::string());
    webrtc_media::MediaError error;
    if (!media_session->SetRecordingDevice(device_id, &error)) {
      context.events.EmitResponse(command.id, command.method, false,
                                  error.code, error.message);
      return;
    }
    {
      std::lock_guard<std::mutex> lock(config_mutex);
      config.recording_device = device_id;
    }
    Json data = {{"device_id", device_id}};
    context.events.EmitNotification("media.recordingDevice", data);
    context.events.EmitResponse(command.id, command.method, true, 0, {},
                                data);
  }

  void HandleSelectPlayoutDevice(const EngineCommand& command) {
    if (!media_session) {
      context.events.EmitResponse(command.id, command.method, false, 409,
                                  "音频媒体未就绪");
      return;
    }
    const std::string device_id =
        command.data.value("device_id", std::string());
    webrtc_media::MediaError error;
    if (!media_session->SetPlayoutDevice(device_id, &error)) {
      context.events.EmitResponse(command.id, command.method, false,
                                  error.code, error.message);
      return;
    }
    {
      std::lock_guard<std::mutex> lock(config_mutex);
      config.playout_device = device_id;
    }
    Json data = {{"device_id", device_id}};
    context.events.EmitNotification("media.playoutDevice", data);
    context.events.EmitResponse(command.id, command.method, true, 0, {},
                                data);
  }

  void HandleSelectSystemAudioDevice(const EngineCommand& command) {
    if (!media_session) {
      context.events.EmitResponse(command.id, command.method, false, 409,
                                  "音频媒体未就绪");
      return;
    }
    const std::string device_id =
        command.data.value("device_id", std::string());
    webrtc_media::MediaError error;
    if (!media_session->SetSystemAudioDevice(device_id, &error)) {
      context.events.EmitResponse(command.id, command.method, false,
                                  error.code, error.message);
      return;
    }
    {
      std::lock_guard<std::mutex> lock(config_mutex);
      config.system_audio_device = device_id;
    }
    Json data = {{"device_id", device_id}};
    context.events.EmitNotification("media.systemAudioDevice", data);
    context.events.EmitResponse(command.id, command.method, true, 0, {},
                                data);
  }

  void HandleSelectScreenSource(const EngineCommand& command) {
    if (!media_session) {
      context.events.EmitResponse(command.id, command.method, false, 409,
                                  "媒体未就绪");
      return;
    }
    const std::string source_id =
        command.data.value("source_id", std::string());
    const bool window = command.data.value("window", false);
    webrtc_media::MediaError error;
    if (!media_session->SetScreenSource(source_id, window, &error)) {
      context.events.EmitResponse(command.id, command.method, false,
                                  error.code, error.message);
      return;
    }
    Json data = {{"source_id", source_id}, {"window", window}};
    context.events.EmitNotification("media.screenSource", data);
    context.events.EmitResponse(command.id, command.method, true, 0, {},
                                data);
  }

  void RestartSfuSend(std::uint64_t generation) {
    context.post_to_worker([this, generation] {
      if (!context.session.IsCurrent(generation) ||
          context.session.media_mode() != SessionMediaMode::Sfu ||
          !media_session) {
        return;
      }
      LOG_INFO("restart SFU send ICE generation={}", generation);
      PublishSfu(generation, true);
    });
  }

  void HandleStats(const EngineCommand& command) {
    if (!media_session) {
      context.events.EmitResponse(command.id, command.method, false, 409,
                                  "音频媒体未就绪");
      return;
    }
    std::string audio_source;
    {
      std::lock_guard<std::mutex> lock(config_mutex);
      audio_source = config.audio_source;
    }
    const auto status = media_session->status();
    const auto now = std::chrono::steady_clock::now();
    const std::uint64_t bytes_sent =
        status.audio_bytes_sent + status.video_bytes_sent;
    const std::uint64_t bytes_received =
        status.audio_bytes_received + status.video_bytes_received;
    double bitrate_bps = 0.0;
    if (last_stats_at_ != std::chrono::steady_clock::time_point{}) {
      const double seconds =
          std::chrono::duration<double>(now - last_stats_at_).count();
      if (seconds > 0.0) {
        const std::uint64_t delta =
            (bytes_sent >= last_bytes_sent_ ? bytes_sent - last_bytes_sent_
                                            : 0) +
            (bytes_received >= last_bytes_received_
                 ? bytes_received - last_bytes_received_
                 : 0);
        bitrate_bps = static_cast<double>(delta) * 8.0 / seconds;
      }
    }
    last_stats_at_ = now;
    last_bytes_sent_ = bytes_sent;
    last_bytes_received_ = bytes_received;
    LOG_INFO(
        "media stats generation={} source={} muted={} recording={} capture_frames={} "
        "sfu={}/{} p2p={}/{} sent={} recv={} remote_tracks={} level={} "
        "camera_frames={} camera_sender={} video_sent={} video_recv={} "
        "screen_frames={} screen_sender={} remote_video_tracks={} "
        "rtt_ms={} jitter_ms={} loss={} bitrate_bps={}",
        context.session.snapshot().generation,
        audio_source, status.muted, status.recording, status.capture_frames,
        status.sfu_send_state, status.sfu_recv_state, status.p2p_state,
        status.p2p_connected, status.p2p_audio_packets_sent,
        status.audio_packets_received, status.remote_tracks,
        status.remote_audio_level, status.camera_capture_frames,
        status.camera_sender_active, status.video_packets_sent,
        status.video_packets_received, status.screen_capture_frames,
        status.screen_sender_active, status.remote_video_tracks,
        status.rtt_ms, status.jitter_ms, status.fraction_lost, bitrate_bps);
    Json data = {
        {"generation", context.session.snapshot().generation},
        {"initialized", status.initialized},
        {"sfu_send_ready", status.sfu_send_ready},
        {"sfu_recv_ready", status.sfu_recv_ready},
        {"p2p_ready", status.p2p_ready},
        {"p2p_connected", status.p2p_connected},
        {"muted", status.muted},
        {"deafened", status.deafened},
        {"sfu_paused", status.sfu_paused},
        {"camera_enabled", status.camera_enabled},
        {"screen_enabled", status.screen_enabled},
        {"recording_initialized", status.recording_initialized},
        {"recording", status.recording},
        {"playout_initialized", status.playout_initialized},
        {"playing", status.playing},
        {"sfu_send_active", status.sfu_send_active},
        {"p2p_send_active", status.p2p_send_active},
        {"camera_sender_active", status.camera_sender_active},
        {"screen_sender_active", status.screen_sender_active},
        {"volume", status.volume},
        {"capture_frames", status.capture_frames},
        {"camera_capture_frames", status.camera_capture_frames},
        {"screen_capture_frames", status.screen_capture_frames},
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
        {"packets_lost", status.packets_lost},
        {"rtt_ms", status.rtt_ms},
        {"jitter_ms", status.jitter_ms},
        {"loss_percent",
         status.fraction_lost >= 0.0 ? status.fraction_lost * 100.0 : -1.0},
        {"bitrate_bps", bitrate_bps},
        {"available_outgoing_bitrate_bps",
         status.available_outgoing_bitrate_bps},
        {"candidate_pair", status.candidate_pair},
        {"remote_audio_level", status.remote_audio_level},
        {"recording_device", status.recording_device},
        {"playout_device", status.playout_device},
        {"sfu_send_state", status.sfu_send_state},
        {"sfu_recv_state", status.sfu_recv_state},
        {"p2p_state", status.p2p_state},
        {"last_error", status.last_error}};
    context.events.EmitResponse(command.id, command.method, true, 0, {}, data);
  }

  void HandleMediaModeRequest(const std::string& request_id, const Json& data,
                              std::uint64_t generation) {
    context.post_to_worker([this, request_id, data, generation] {
      if (!context.session.IsCurrent(generation)) return;
      const std::string mode =
          data.value("mode", data.value("media_mode", std::string()));
      LOG_INFO("media mode request id={} mode={}", request_id, mode);
      if (!media_session) {
        context.signaling.Send(
            signaling::Failure(request_id, 409, "音频媒体未就绪"));
        return;
      }
      if (mode == "p2p") {
        const Json allocation = data.value("p2p", Json::object());
        if (!allocation.is_object() || allocation.empty()) {
          context.signaling.Send(
              signaling::Failure(request_id, 400, "缺少 P2P 分配"));
          return;
        }
        pending_sfu_request_id.clear();
        pending_sfu_room_mode = Json::object();
        sfu_publish_ready = false;
        sfu_subscribe_ready = false;
        context.signaling.Post([this] { media_mode_timer.cancel(); });
        p2p_session = allocation.value("session", std::string());
        p2p_peer = allocation.value("peer", std::string());
        p2p_offerer =
            allocation.value("role", std::string()) == "offerer";
        p2p_video_allowed = data.value("p2p_video", false);
        if (media_session) media_session->SetP2pPeerUid(p2p_peer);
        if (!context.session.SetMediaMode(generation,
                                          SessionMediaMode::P2p)) {
          context.signaling.Send(
              signaling::Failure(request_id, 409, "会话已失效"));
          return;
        }
        StartP2p(generation);
        context.events.EmitNotification("room.mediaMode", data);
        context.signaling.Send(
            signaling::Response(request_id, Json::object()));
        return;
      }
      if (mode == "sfu") {
        pending_sfu_request_id = request_id;
        pending_sfu_room_mode = data;
        sfu_publish_ready = false;
        sfu_subscribe_ready = false;
        PublishSfu(generation);
        if (pending_sfu_request_id == request_id) {
          ScheduleSfuSwitchTimeout(request_id, generation);
        }
        return;
      }
      context.signaling.Send(
          signaling::Failure(request_id, 400, "媒体模式无效"));
    });
  }

  void HandleSfuOfferRequest(const std::string& request_id, const Json& data,
                             std::uint64_t generation) {
    const std::string sdp = data.value("sdp", std::string());
    if (sdp.empty()) {
      context.signaling.Send(
          signaling::Failure(request_id, 400, "缺少 SDP"));
      return;
    }
    const auto consumers =
        ParseRemoteTracks(data.value("consumers", Json::array()));
    context.post_to_worker([this, request_id, sdp, consumers, generation] {
      if (!context.session.IsCurrent(generation)) return;
      if (!media_session) {
        context.signaling.Send(
            signaling::Failure(request_id, 409, "音频媒体未就绪"));
        return;
      }
      std::string answer;
      webrtc_media::MediaError error;
      const bool ok = media_session->CreateSfuSubscribeAnswer(
          sdp, consumers, &answer, &error);
      if (ok) {
        for (const auto& consumer : consumers) {
          Json track = {{"pc", "sfu-recv"},
                        {"mid", consumer.mid},
                        {"uid", consumer.uid},
                        {"kind", consumer.kind},
                        {"source", consumer.source}};
          context.events.EmitNotification("media.track", track);
        }
        context.signaling.Send(
            signaling::Response(request_id, Json{{"sdp", answer}}));
        context.post_to_worker([this, generation] {
          if (!context.session.IsCurrent(generation)) return;
          sfu_subscribe_ready = true;
          if (!pending_sfu_request_id.empty()) {
            TryCompleteSfuSwitch(false, generation);
          }
        });
      } else {
        context.signaling.Send(signaling::Failure(
            request_id, error.code == 0 ? 500 : error.code, error.message));
        context.post_to_worker([this, error, generation] {
          if (!context.session.IsCurrent(generation)) return;
          if (!pending_sfu_request_id.empty()) {
            FailPendingSfuSwitch(
                error.code == 0 ? 500 : error.code,
                error.message.empty() ? "SFU subscribe 失败"
                                      : error.message,
                generation);
          }
        });
      }
    });
  }

  bool HandleSignalingEvent(const std::string& method, const Json& data,
                            std::uint64_t generation) {
    if (method == "floor.state" || method == "floor.revoked") {
      context.post_to_worker([this, method, data, generation] {
        if (!context.session.IsCurrent(generation)) return;
        std::string self_uid;
        {
          std::lock_guard<std::mutex> lock(config_mutex);
          self_uid = local_uid;
        }
        std::string holder;
        if (method == "floor.state" && data.contains("holder") &&
            !data["holder"].is_null()) {
          holder = data.value("holder", std::string());
        }
        const bool open =
            method == "floor.state" && !self_uid.empty() &&
            holder == self_uid;
        if (media_session != nullptr) {
          webrtc_media::MediaError error;
          if (!media_session->SetMuted(!open, &error)) {
            LOG_WARN("floor local mute failed open={} error={}", open,
                     error.message);
          }
        }
        context.events.EmitNotification(
            "floor.local",
            Json{{"open", open},
                 {"reason", data.value("reason", std::string())}});
      });
      return false;
    }
    if (method == "p2p.offer") {
      context.post_to_worker([this, data, generation] {
        if (!context.session.IsCurrent(generation)) return;
        ApplyP2pOffer(data.value("session", std::string()),
                      data.value("from", std::string()),
                      data.value("sdp", std::string()), generation);
      });
      return true;
    }
    if (method == "p2p.answer") {
      context.post_to_worker([this, data, generation] {
        if (!context.session.IsCurrent(generation)) return;
        ApplyP2pAnswer(data.value("session", std::string()),
                       data.value("sdp", std::string()), generation);
      });
      return true;
    }
    if (method == "p2p.candidate") {
      context.post_to_worker([this, data, generation] {
        if (!context.session.IsCurrent(generation)) return;
        ApplyP2pCandidate(data.value("session", std::string()),
                          data.value("candidate", std::string()),
                          data.value("sdp_mid", std::string("0")),
                          data.value("sdp_mline_index", 0), generation);
      });
      return true;
    }
    if (method == "p2p.close") {
      context.post_to_worker([this, generation] {
        CloseP2p(generation);
      });
      return true;
    }
    if (method == "media.forceMute") {
      context.post_to_worker([this, generation, data] {
        if (!context.session.IsCurrent(generation)) return;
        const std::string source =
            data.value("source", std::string("mic"));
        webrtc_media::MediaError error;
        if (source == "screen") {
          if (media_session) media_session->SetScreenEnabled(false, &error);
          screen_generation.fetch_add(1);
          const auto status =
              media_session ? media_session->status()
                            : webrtc_media::MediaStatus{};
          context.events.EmitNotification(
              "video.state",
              Json{{"source", "screen"},
                   {"sharing", false},
                   {"screen", false},
                   {"camera", status.camera_enabled},
                   {"on", status.camera_enabled},
                   {"forced", true}});
          return;
        }
        if (source == "camera") {
          camera_publishing.store(false);
          camera_generation.fetch_add(1);
          if (media_session) media_session->SetCameraEnabled(false, &error);
          StopCameraCaptureIfIdle();
          const auto status =
              media_session ? media_session->status()
                            : webrtc_media::MediaStatus{};
          context.events.EmitNotification(
              "video.state",
              Json{{"source", "camera"},
                   {"sharing", false},
                   {"camera", false},
                   {"screen", status.screen_enabled},
                   {"on", status.screen_enabled},
                   {"forced", true}});
          return;
        }
        if (media_session) media_session->SetMuted(true, &error);
        context.events.EmitNotification(
            "media.muted", Json{{"muted", true}, {"forced", true}});
      });
      return true;
    }
    return false;
  }

  void HandleMediaEvent(const std::string& method, const Json& input,
                        std::uint64_t generation) {
    if (!context.session.IsCurrent(generation)) return;
    Json data = input;
    if (method == "p2p.candidate") {
      if (p2p_session.empty()) return;
      Json candidate = data;
      candidate.erase("pc");
      candidate["session"] = p2p_session;
      candidate["to"] = p2p_peer;
      context.signaling.Notify("p2p.candidate", candidate);
      return;
    }
    if (method == "audio.connection") {
      const std::string pc = data.value("pc", std::string());
      const std::string state = data.value("state", std::string());
      LOG_INFO("audio connection pc={} state={}", pc, state);
      if (pc == "p2p") {
        if (state == "connected") {
          sfu_paused = true;
          if (media_session) {
            webrtc_media::MediaError error;
            media_session->SetSfuPaused(true, &error);
          }
        } else if (state == "failed" || state == "closed") {
          if (sfu_paused) {
            sfu_paused = false;
            if (media_session) {
              webrtc_media::MediaError error;
              media_session->SetSfuPaused(false, &error);
            }
          }
        }
        Json out = {{"session", p2p_session}, {"state", state}};
        context.signaling.Notify("p2p.state", out);
      }
      context.events.EmitNotification("media.state", data);
      return;
    }
    if (method == "media.track" || method == "audio.track") {
      Json out = data;
      out["generation"] = generation;
      if (data.value("pc", std::string()) == "p2p") {
        out["uid"] = p2p_peer;
        if (out.value("kind", std::string()) == "video" &&
            !out.contains("source")) {
          out["source"] = "camera";
        }
      }
      context.events.EmitNotification("media.track", out);
    }
  }

  bool EnsureCameraCapture(std::string* error) {
    if (camera.running()) return true;
    camera_win::CameraCaptureConfig capture_config;
    capture_config.device_index = camera_device_index;
    return camera.Start(
        capture_config,
        [this](const camera_win::I420Frame& frame) { OnCameraFrame(frame); },
        [this](const std::string& message) {
          context.events.EmitNotification(
              "camera.preview.error", Json{{"message", message}});
          context.post_to_worker([this, message] {
            if (!camera_preview_active && !camera_publishing.load()) return;
            const auto disable_server_camera = [this] {
              const auto session = context.session.snapshot();
              if (session.phase != SessionPhase::Joined) return;
              context.signaling.Request(
                  "media.setMute",
                  Json{{"source", "camera"}, {"muted", true}},
                  [](Json) {});
            };
            const auto devices = camera_win::CameraService::Enumerate();
            if (devices.empty()) {
              camera_preview_active = false;
              camera_publishing.store(false);
              disable_server_camera();
              if (media_session) {
                webrtc_media::MediaError error;
                media_session->SetCameraEnabled(false, &error);
              }
              context.events.EmitNotification(
                  "video.state",
                  Json{{"source", "camera"},
                       {"sharing", false},
                       {"camera", false},
                       {"forced", true},
                       {"reason", message}});
              return;
            }
            int next = devices.front().index;
            for (const auto& device : devices) {
              if (device.index != camera_device_index) {
                next = device.index;
                break;
              }
            }
            camera.Stop();
            camera_device_index = next;
            std::string recovery_error;
            if (!EnsureCameraCapture(&recovery_error)) {
              camera_preview_active = false;
              camera_publishing.store(false);
              disable_server_camera();
              if (media_session) {
                webrtc_media::MediaError error;
                media_session->SetCameraEnabled(false, &error);
              }
              context.events.EmitNotification(
                  "video.state",
                  Json{{"source", "camera"},
                       {"sharing", false},
                       {"camera", false},
                       {"forced", true},
                       {"reason", recovery_error}});
              return;
            }
            context.events.EmitNotification(
                "camera.deviceRecovered",
                Json{{"device_index", camera_device_index},
                     {"device", camera.device_name()}});
          });
        },
        error);
  }

  void StopCameraCaptureIfIdle() {
    if (!camera_preview_active && !camera_publishing.load()) {
      camera.Stop();
    }
  }

  void OnCameraFrame(const camera_win::I420Frame& frame) {
    if (camera_publishing.load(std::memory_order_acquire) && media_session) {
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
      if (!media_session->PushCameraFrame(data, &error)) {
        context.events.EmitNotification(
            "media.error",
            Json{{"code", error.code},
                 {"message", "摄像头帧送入编码器失败"},
                 {"detail", error.message}});
      }
    }
    if (context.options.on_local_frame == nullptr) return;
    std::string uid;
    {
      std::lock_guard<std::mutex> lock(config_mutex);
      uid = local_uid;
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
    context.options.on_local_frame(context.options.userdata, &view);
  }

  void OnLocalScreenFrame(const webrtc_media::VideoFrameData& frame,
                          std::uint64_t generation) {
    if (!context.session.IsCurrent(generation)) return;
    if (context.options.on_local_frame == nullptr) return;
    std::string uid;
    {
      std::lock_guard<std::mutex> lock(config_mutex);
      uid = local_uid;
    }
    rtcwin_frame_view view{};
    view.uid = uid.c_str();
    view.mid = "local:screen";
    view.i420_y = frame.data_y;
    view.i420_u = frame.data_u;
    view.i420_v = frame.data_v;
    view.width = frame.width;
    view.height = frame.height;
    view.stride_y = frame.stride_y;
    view.stride_u = frame.stride_u;
    view.stride_v = frame.stride_v;
    view.timestamp_us = frame.timestamp_us;
    view.source = 1;
    context.options.on_local_frame(context.options.userdata, &view);
  }

  void OnRemoteVideoFrame(const webrtc_media::RemoteVideoFrame& frame,
                          std::uint64_t generation) {
    if (!context.session.IsCurrent(generation)) return;
    if (context.options.on_remote_frame == nullptr) return;
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
    context.options.on_remote_frame(context.options.userdata, &view);
  }

  void PublishSfu(std::uint64_t generation, bool ice_restart = false) {
    if (!context.session.IsCurrent(generation)) return;
    if (!media_session) {
      EmitMediaError(1730, "音频媒体未就绪", "SFU 发布被跳过");
      return;
    }
    webrtc_media::MediaSfuOffer offer;
    webrtc_media::MediaError error;
    if (!media_session->CreateSfuPublishOffer(&offer, &error, ice_restart)) {
      LOG_ERR("sfu offer failed code={} error={}", error.code,
              error.message);
      if (!pending_sfu_request_id.empty()) {
        FailPendingSfuSwitch(error.code == 0 ? 1730 : error.code,
                             error.message.empty()
                                 ? "生成 SFU 音频 offer 失败"
                                 : error.message,
                             generation);
      } else {
        EmitMediaError(error.code, "生成 SFU 音频 offer 失败",
                       error.message);
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
    if (!offer.screen_mid.empty()) {
      tracks.push_back({{"mid", offer.screen_mid},
                        {"kind", "video"},
                        {"source", "screen"}});
    }
    Json request = {{"sdp", offer.sdp}, {"tracks", tracks}};
    LOG_INFO("sfu publish offer source={} audio_mid={} camera_mid={} "
             "screen_mid={} sdp_bytes={} ice_restart={}",
             offer.source, offer.audio_mid, offer.camera_mid,
             offer.screen_mid, offer.sdp.size(), ice_restart);
    context.signaling.Request(
        "sfu.publish", request, [this, generation](Json result) {
          if (!context.session.IsCurrent(generation)) return;
          if (!result.value("ok", false)) {
            const int code = ResponseCode(result);
            const std::string message = ResponseMessage(result);
            LOG_ERR("sfu publish rejected code={} message={}", code,
                    message);
            context.post_to_worker([this, generation, code, message] {
              if (!context.session.IsCurrent(generation)) return;
              if (!pending_sfu_request_id.empty()) {
                FailPendingSfuSwitch(code, message, generation);
              } else {
                EmitMediaError(code, "SFU 音频发布失败", message);
              }
            });
            return;
          }
          const std::string answer = result.value("data", Json::object())
                                         .value("sdp", std::string());
          context.post_to_worker([this, generation, answer] {
            if (!context.session.IsCurrent(generation)) return;
            ApplySfuAnswer(answer, generation);
          });
        });
    context.events.EmitNotification(
        "media.publishing",
        Json{{"mode", "sfu"}, {"sdp_bytes", offer.sdp.size()}});
  }

  void ApplySfuAnswer(const std::string& sdp, std::uint64_t generation) {
    if (!context.session.IsCurrent(generation) || !media_session) return;
    if (sdp.empty()) {
      if (!pending_sfu_request_id.empty()) {
        FailPendingSfuSwitch(1731, "SFU publish answer 为空", generation);
      } else {
        EmitMediaError(1731, "应用 SFU answer 失败", "SDP 为空");
      }
      return;
    }
    webrtc_media::MediaError error;
    if (!media_session->ApplySfuPublishAnswer(sdp, &error)) {
      if (!pending_sfu_request_id.empty()) {
        FailPendingSfuSwitch(error.code, error.message, generation);
      } else {
        EmitMediaError(error.code, "应用 SFU answer 失败", error.message);
      }
      return;
    }
    sfu_publish_ready = true;
    sfu_paused = !p2p_session.empty();
    LOG_INFO("sfu publish answer applied sdp_bytes={} paused={}", sdp.size(),
             sfu_paused);
    if (!media_session->SetSfuPaused(sfu_paused, &error)) {
      EmitMediaError(error.code, "更新 SFU 暂停状态失败", error.message);
    }
    // The first sfu.publish creates video producers before the room-level
    // camera/screen intent is synchronized. Re-apply both source states so a
    // disabled screen producer never appears as an active share to receivers.
    const auto sync_video_source = [this, generation](
                                       const char* source, bool enabled) {
      context.signaling.Request(
          "media.setMute",
          Json{{"source", source}, {"muted", !enabled}},
          [this, generation, source](Json result) {
            if (!context.session.IsCurrent(generation)) return;
            if (!result.value("ok", false)) {
              LOG_WARN("synchronize {} source failed code={} message={}",
                       source, ResponseCode(result), ResponseMessage(result));
            }
          });
    };
    sync_video_source("camera", camera_publishing.load());
    sync_video_source("screen",
                      media_session && media_session->status().screen_enabled);
    context.events.EmitNotification(
        "media.published",
        Json{{"ok", true}, {"mode", "sfu"}, {"generation", generation}});
    if (!pending_sfu_request_id.empty()) {
      TryCompleteSfuSwitch(false, generation);
    }
  }

  void StartP2p(std::uint64_t generation) {
    if (!context.session.IsCurrent(generation) || !media_session) {
      EmitMediaError(1731, "音频媒体未就绪", "P2P 初始化被跳过");
      return;
    }
    if (p2p_session.empty() || p2p_peer.empty()) {
      LOG_WARN("p2p allocation missing, falling back to sfu");
      context.events.EmitNotification(
          "p2p.state",
          Json{{"state", "failed"}, {"reason", "missing_allocation"}});
      EmitMediaError(1732, "P2P 分配参数缺失", "回退到 SFU 音频");
      if (!context.session.SetMediaMode(generation,
                                        SessionMediaMode::Sfu)) {
        return;
      }
      PublishSfu(generation);
      return;
    }
    if (!p2p_offerer) {
      LOG_INFO("p2p awaiting offer session={} peer={}", p2p_session,
               p2p_peer);
      context.events.EmitNotification(
          "p2p.state",
          Json{{"session", p2p_session}, {"state", "awaiting_offer"}});
      return;
    }
    std::string offer;
    webrtc_media::MediaError error;
    if (!media_session->CreateP2pOffer(p2p_video_allowed, &offer, &error)) {
      LOG_ERR("p2p offer failed code={} error={}", error.code,
              error.message);
      EmitMediaError(error.code, "创建 P2P 音频 offer 失败",
                     error.message);
      context.events.EmitNotification(
          "p2p.state",
          Json{{"session", p2p_session}, {"state", "failed"}});
      return;
    }
    Json event = {
        {"session", p2p_session}, {"to", p2p_peer}, {"sdp", offer}};
    LOG_INFO("p2p offer sent session={} peer={} sdp_bytes={}",
             p2p_session, p2p_peer, offer.size());
    context.signaling.Notify("p2p.offer", event);
    context.events.EmitNotification(
        "p2p.started",
        Json{{"session", p2p_session},
             {"peer", p2p_peer},
             {"role", "offerer"}});
  }

  void ApplyP2pOffer(const std::string& session, const std::string& from,
                     const std::string& sdp, std::uint64_t generation) {
    if (!context.session.IsCurrent(generation) || session.empty() ||
        from.empty() || sdp.empty()) {
      return;
    }
    LOG_INFO("p2p offer received session={} from={} sdp_bytes={}", session,
             from, sdp.size());
    if (p2p_session.empty()) {
      p2p_session = session;
      p2p_peer = from;
      p2p_offerer = false;
      context.session.SetMediaMode(generation, SessionMediaMode::P2p);
    }
    if (session != p2p_session || !media_session) return;
    p2p_peer = from;
    p2p_offerer = false;
    std::string answer;
    webrtc_media::MediaError error;
    if (!media_session->CreateP2pAnswer(sdp, p2p_video_allowed, &answer,
                                        &error)) {
      LOG_ERR("p2p answer create failed session={} code={} error={}",
              session, error.code, error.message);
      EmitMediaError(error.code, "创建 P2P 音频 answer 失败",
                     error.message);
      context.events.EmitNotification(
          "p2p.state",
          Json{{"session", session}, {"state", "failed"}});
      return;
    }
    Json event = {{"session", session}, {"to", from}, {"sdp", answer}};
    context.signaling.Notify("p2p.answer", event);
    context.events.EmitNotification(
        "p2p.started",
        Json{{"session", session}, {"peer", from}, {"role", "answerer"}});
  }

  void ApplyP2pAnswer(const std::string& session, const std::string& sdp,
                      std::uint64_t generation) {
    if (!context.session.IsCurrent(generation) ||
        session != p2p_session || !p2p_offerer || sdp.empty() ||
        !media_session) {
      return;
    }
    webrtc_media::MediaError error;
    if (!media_session->ApplyP2pAnswer(sdp, &error)) {
      LOG_ERR("p2p answer apply failed session={} code={} error={}",
              session, error.code, error.message);
      EmitMediaError(error.code, "应用 P2P 音频 answer 失败",
                     error.message);
      context.events.EmitNotification(
          "p2p.state",
          Json{{"session", session}, {"state", "failed"}});
    }
  }

  void ApplyP2pCandidate(const std::string& session,
                         const std::string& candidate,
                         const std::string& mid, int index,
                         std::uint64_t generation) {
    if (!context.session.IsCurrent(generation) ||
        session != p2p_session || !media_session || candidate.empty()) {
      return;
    }
    webrtc_media::MediaError error;
    if (!media_session->AddP2pCandidate(candidate, mid, index, &error)) {
      EmitMediaError(error.code, "添加 P2P ICE 候选失败", error.message);
    }
  }

  void CloseP2p(std::uint64_t generation) {
    if (!context.session.IsCurrent(generation)) return;
    if (media_session) media_session->CloseP2p();
    p2p_session.clear();
    p2p_peer.clear();
    p2p_offerer = false;
    if (sfu_paused) {
      sfu_paused = false;
      if (media_session) {
        webrtc_media::MediaError error;
        media_session->SetSfuPaused(false, &error);
      }
    }
  }

  void TryCompleteSfuSwitch(bool timed_out, std::uint64_t generation) {
    if (!context.session.IsCurrent(generation)) return;
    if (pending_sfu_request_id.empty()) return;
    if (timed_out) {
      FailPendingSfuSwitch(504, "SFU 切换超时，保留当前 P2P 音频",
                           generation);
      return;
    }
    if (!sfu_publish_ready || !sfu_subscribe_ready) return;

    const std::string request_id = pending_sfu_request_id;
    const Json room_mode = pending_sfu_room_mode;
    pending_sfu_request_id.clear();
    pending_sfu_room_mode = Json::object();
    sfu_publish_ready = false;
    sfu_subscribe_ready = false;
    context.signaling.Post([this] { media_mode_timer.cancel(); });

    if (media_session) media_session->CloseP2p();
    p2p_session.clear();
    p2p_peer.clear();
    p2p_offerer = false;
    sfu_paused = false;
    webrtc_media::MediaError error;
    if (media_session && !media_session->SetSfuPaused(false, &error)) {
      EmitMediaError(error.code, "恢复 SFU 音频失败", error.message);
    }
    if (!context.session.SetMediaMode(generation, SessionMediaMode::Sfu)) {
      return;
    }
    context.events.EmitNotification(
        "room.mediaMode",
        room_mode.empty() ? Json{{"mode", "sfu"}} : room_mode);
    context.signaling.Send(
        signaling::Response(request_id, Json::object()));
  }

  void FailPendingSfuSwitch(int code, const std::string& message,
                            std::uint64_t generation) {
    if (!context.session.IsCurrent(generation)) return;
    if (pending_sfu_request_id.empty()) return;
    const std::string request_id = pending_sfu_request_id;
    pending_sfu_request_id.clear();
    pending_sfu_room_mode = Json::object();
    sfu_publish_ready = false;
    sfu_subscribe_ready = false;
    context.signaling.Post([this] { media_mode_timer.cancel(); });

    if (!p2p_session.empty()) {
      sfu_paused = true;
      if (media_session) {
        webrtc_media::MediaError ignored;
        media_session->SetSfuPaused(true, &ignored);
      }
    }
    EmitMediaError(code == 0 ? 1732 : code, "SFU 媒体切换失败", message);
    context.signaling.Send(signaling::Failure(
        request_id, code == 0 ? 500 : code,
        message.empty() ? "SFU 媒体切换失败" : message));
  }

  void ScheduleSfuSwitchTimeout(const std::string& request_id,
                                std::uint64_t generation) {
    context.signaling.Post([this, request_id, generation] {
      media_mode_timer.expires_after(std::chrono::seconds(5));
      media_mode_timer.async_wait(
          [this, request_id, generation](const asio::error_code& error) {
            if (error) return;
            context.post_to_worker([this, request_id, generation] {
              if (context.session.IsCurrent(generation) &&
                  pending_sfu_request_id == request_id) {
                TryCompleteSfuSwitch(true, generation);
              }
            });
          });
    });
  }

  void EmitMediaError(int code, const std::string& message,
                      const std::string& detail) {
    context.events.EmitNotification(
        "media.error",
        Json{{"code", code}, {"message", message}, {"detail", detail}});
  }

  Context context;
  mutable std::mutex config_mutex;
  webrtc_media::MediaConfig config;
  std::string local_uid;
  std::unique_ptr<webrtc_media::MediaSession> media_session;
  std::atomic<std::uint64_t> mute_generation{0};
  std::atomic<std::uint64_t> camera_generation{0};
  std::atomic<std::uint64_t> screen_generation{0};
  std::chrono::steady_clock::time_point last_stats_at_{};
  std::uint64_t last_bytes_sent_ = 0;
  std::uint64_t last_bytes_received_ = 0;
  camera_win::CameraCaptureSession camera;
  bool camera_preview_active = false;
  std::atomic<bool> camera_publishing{false};
  bool camera_toggle_pending = false;
  bool screen_toggle_pending = false;
  int camera_device_index = -1;
  std::string p2p_session;
  std::string p2p_peer;
  bool p2p_offerer = false;
  bool p2p_video_allowed = false;
  bool sfu_paused = false;
  bool sfu_publish_ready = false;
  bool sfu_subscribe_ready = false;
  std::string pending_sfu_request_id;
  Json pending_sfu_room_mode;
  bool deafened = false;
  asio::steady_timer media_mode_timer;
};

MediaOrchestrator::MediaOrchestrator(Context context)
    : impl_(std::make_unique<Impl>(std::move(context))) {}

MediaOrchestrator::~MediaOrchestrator() = default;

void MediaOrchestrator::Configure(
    const webrtc_media::MediaConfig& config, std::string local_uid) {
  impl_->Configure(config, std::move(local_uid));
}

void MediaOrchestrator::SetIceServers(
    std::vector<webrtc_media::IceServer> ice_servers) {
  impl_->SetIceServers(std::move(ice_servers));
}

bool MediaOrchestrator::audio_requested() const {
  return impl_->audio_requested();
}

bool MediaOrchestrator::Initialize(const signaling::Json& join_data,
                                   std::uint64_t generation,
                                   webrtc_media::MediaError* error) {
  return impl_->Initialize(join_data, generation, error);
}

void MediaOrchestrator::Start(std::uint64_t generation) {
  impl_->Start(generation);
}

void MediaOrchestrator::Reset() { impl_->Reset(); }

void MediaOrchestrator::HandleCameraDevices(const EngineCommand& command) {
  impl_->HandleCameraDevices(command);
}

void MediaOrchestrator::HandleCameraSelect(const EngineCommand& command) {
  impl_->HandleCameraSelect(command);
}

void MediaOrchestrator::HandleCameraPreviewStart(
    const EngineCommand& command) {
  impl_->HandleCameraPreviewStart(command);
}

void MediaOrchestrator::HandleCameraPreviewStop(
    const EngineCommand& command) {
  impl_->HandleCameraPreviewStop(command);
}

void MediaOrchestrator::HandleSetMute(const EngineCommand& command) {
  impl_->HandleSetMute(command);
}

void MediaOrchestrator::HandleToggleCamera(const EngineCommand& command) {
  impl_->HandleToggleCamera(command);
}

void MediaOrchestrator::HandleToggleScreenShare(
    const EngineCommand& command) {
  impl_->HandleToggleScreenShare(command);
}

void MediaOrchestrator::HandleSetVolume(const EngineCommand& command) {
  impl_->HandleSetVolume(command);
}

void MediaOrchestrator::HandleSetDeafened(const EngineCommand& command) {
  impl_->HandleSetDeafened(command);
}

void MediaOrchestrator::HandleSelectRecordingDevice(
    const EngineCommand& command) {
  impl_->HandleSelectRecordingDevice(command);
}

void MediaOrchestrator::HandleSelectPlayoutDevice(
    const EngineCommand& command) {
  impl_->HandleSelectPlayoutDevice(command);
}

void MediaOrchestrator::HandleSelectSystemAudioDevice(
    const EngineCommand& command) {
  impl_->HandleSelectSystemAudioDevice(command);
}

void MediaOrchestrator::HandleSelectScreenSource(
    const EngineCommand& command) {
  impl_->HandleSelectScreenSource(command);
}

void MediaOrchestrator::RestartSfuSend(std::uint64_t generation) {
  impl_->RestartSfuSend(generation);
}

void MediaOrchestrator::HandleStats(const EngineCommand& command) {
  impl_->HandleStats(command);
}

void MediaOrchestrator::HandleMediaModeRequest(
    const std::string& request_id, const signaling::Json& data,
    std::uint64_t generation) {
  impl_->HandleMediaModeRequest(request_id, data, generation);
}

void MediaOrchestrator::HandleSfuOfferRequest(
    const std::string& request_id, const signaling::Json& data,
    std::uint64_t generation) {
  impl_->HandleSfuOfferRequest(request_id, data, generation);
}

bool MediaOrchestrator::HandleSignalingEvent(
    const std::string& method, const signaling::Json& data,
    std::uint64_t generation) {
  return impl_->HandleSignalingEvent(method, data, generation);
}

void MediaOrchestrator::HandleMediaEvent(const std::string& method,
                                         const signaling::Json& data,
                                         std::uint64_t generation) {
  impl_->HandleMediaEvent(method, data, generation);
}

}  // namespace rtcwin::engine
