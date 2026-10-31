#pragma once
/**
 * @brief:摄像头、媒体命令、SFU/P2P 协商、媒体事件
*/
#include "rtcwin/client_api.h"
#include "rtcwin/engine/engine_command.h"
#include "rtcwin/engine/engine_event_sink.h"
#include "rtcwin/engine/session_state_machine.h"
#include "rtcwin/engine/signaling_transport.h"
#include "rtcwin/signaling/json.h"
#include "rtcwin/webrtc_media/media_session.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace rtcwin::engine {

// Owns camera capture, WebRTC media negotiation and media command handling.
// EngineCore only wires threads, session lifecycle and command routing.
class MediaOrchestrator final {
 public:
  using TaskPoster = std::function<void(std::function<void()>)>;

  struct Context {
    const rtcwin_options& options;
    SignalingTransport& signaling;
    SessionStateMachine& session;
    EngineEventSink& events;
    TaskPoster post_to_worker;
  };

  explicit MediaOrchestrator(Context context);
  ~MediaOrchestrator();

  MediaOrchestrator(const MediaOrchestrator&) = delete;
  MediaOrchestrator& operator=(const MediaOrchestrator&) = delete;

  void Configure(const webrtc_media::MediaConfig& config,
                 std::string local_uid);
  void SetIceServers(std::vector<webrtc_media::IceServer> ice_servers);
  bool audio_requested() const;

  bool Initialize(const signaling::Json& join_data, std::uint64_t generation,
                  webrtc_media::MediaError* error);
  void Start(std::uint64_t generation);
  void Reset();

  void HandleCameraDevices(const EngineCommand& command);
  void HandleCameraSelect(const EngineCommand& command);
  void HandleCameraPreviewStart(const EngineCommand& command);
  void HandleCameraPreviewStop(const EngineCommand& command);
  void HandleSetMute(const EngineCommand& command);
  void HandleToggleCamera(const EngineCommand& command);
  void HandleToggleScreenShare(const EngineCommand& command);
  void HandleSetVolume(const EngineCommand& command);
  void HandleSetDeafened(const EngineCommand& command);
  void HandleSelectRecordingDevice(const EngineCommand& command);
  void HandleSelectPlayoutDevice(const EngineCommand& command);
  void HandleSelectSystemAudioDevice(const EngineCommand& command);
  void HandleSelectScreenSource(const EngineCommand& command);
  void HandleStats(const EngineCommand& command);

  void HandleMediaModeRequest(const std::string& request_id,
                              const signaling::Json& data,
                              std::uint64_t generation);
  void HandleSfuOfferRequest(const std::string& request_id,
                             const signaling::Json& data,
                             std::uint64_t generation);
  bool HandleSignalingEvent(const std::string& method,
                            const signaling::Json& data,
                            std::uint64_t generation);
  void HandleMediaEvent(const std::string& method,
                        const signaling::Json& data,
                        std::uint64_t generation);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace rtcwin::engine
