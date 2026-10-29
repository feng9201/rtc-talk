#pragma once

#include "rtcwin/camera_win/camera_capture.h"
#include "rtcwin/client_api.h"
#include "rtcwin/engine/engine_command.h"
#include "rtcwin/engine/engine_command_router.h"
#include "rtcwin/engine/engine_event_sink.h"
#include "rtcwin/engine/session_state_machine.h"
#include "rtcwin/engine/signaling_transport.h"
#include "rtcwin/signaling/signaling_client.h"
#include "rtcwin/webrtc_media/media_session.h"

#include <asio.hpp>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace rtcwin::engine {

class EngineCore final {
 public:
  EngineCore(const rtcwin_options& options, std::string config_json);
  ~EngineCore();

  EngineCore(const EngineCore&) = delete;
  EngineCore& operator=(const EngineCore&) = delete;

  uint64_t Submit(std::string method, std::string json);

 private:
  struct Command {
    uint64_t id = 0;
    std::string method;
    signaling::Json data = signaling::Json::object();
    std::string parse_error;
    std::function<void()> task;
  };

  void PostToWorker(std::function<void()> task);
  void PostToSignaling(std::function<void()> task);
  void WorkerLoop();
  void RegisterCommandHandlers();
  void HandleCameraDevices(const EngineCommand& command);
  void HandleCameraPreviewStart(const EngineCommand& command);
  void HandleCameraPreviewStop(const EngineCommand& command);
  void HandleAudioDevices(const EngineCommand& command);
  void HandleSessionConnectAndJoin(const EngineCommand& command);
  void HandleMediaSetMute(const EngineCommand& command);
  void HandleMediaToggleCamera(const EngineCommand& command);
  void HandleMediaToggleScreenShare(const EngineCommand& command);
  void HandleMediaSetVolume(const EngineCommand& command);
  void HandleMediaSetDeafened(const EngineCommand& command);
  void HandleMediaStats(const EngineCommand& command);
  void HandleRoomLeave(const EngineCommand& command);
  void HandleSessionDisconnect(const EngineCommand& command);
  void ScheduleHeartbeat(int seconds, std::uint64_t generation);
  void OnCameraFrame(const camera_win::I420Frame& frame);
  void OnRemoteVideoFrame(const webrtc_media::RemoteVideoFrame& frame,
                          std::uint64_t generation);
  bool EnsureCameraCapture(std::string* error);
  void StopCameraCaptureIfIdle();

  void FinalizeJoin(uint64_t command_id, const signaling::Json& data,
                    std::uint64_t generation);
  bool InitializeMedia(const signaling::Json& join_data,
                       std::string* error, std::uint64_t generation);
  void StartMediaForCurrentMode(std::uint64_t generation);
  void PublishSfu(std::uint64_t generation);
  void ApplySfuAnswer(const std::string& sdp, std::uint64_t generation);
  void StartP2p(std::uint64_t generation);
  void ApplyP2pOffer(const std::string& session, const std::string& from,
                     const std::string& sdp, std::uint64_t generation);
  void ApplyP2pAnswer(const std::string& session, const std::string& sdp,
                      std::uint64_t generation);
  void ApplyP2pCandidate(const std::string& session,
                         const std::string& candidate,
                         const std::string& mid, int index,
                         std::uint64_t generation);
  void HandleMediaModeRequest(const std::string& request_id,
                              const signaling::Json& data,
                              std::uint64_t generation);
  void HandleSfuOfferRequest(const std::string& request_id,
                             const signaling::Json& data,
                             std::uint64_t generation);
  void TryCompleteSfuSwitch(bool timed_out, std::uint64_t generation);
  void FailPendingSfuSwitch(int code, const std::string& message,
                            std::uint64_t generation);
  void ScheduleSfuSwitchTimeout(const std::string& request_id,
                                std::uint64_t generation);
  void HandleMediaEvent(const std::string& method,
                        const std::string& data_json,
                        std::uint64_t generation);
  void ResetMedia();
  void EmitMediaError(int code, const std::string& message,
                      const std::string& detail);

  void EmitResponse(uint64_t id, const std::string& method, bool ok,
                    int error_code, const std::string& message,
                    const signaling::Json& data = signaling::Json::object());
  void EmitNotification(const std::string& method,
                        const signaling::Json& data =
                            signaling::Json::object());

  rtcwin_options options_{};
  EngineEventSink events_;
  EngineCommandRouter command_router_;
  std::string config_json_;
  std::atomic<uint64_t> next_id_{1};
  std::thread worker_;
  std::mutex mutex_;
  std::condition_variable cv_;
  std::deque<Command> commands_;
  bool stopping_ = false;
  SignalingTransport signaling_transport_;
  SessionStateMachine session_state_;
  std::mutex session_mutex_;
  std::string session_url_;
  std::string session_token_;
  std::string session_uid_;
  std::string session_room_id_;
  std::string session_display_;
  webrtc_media::MediaConfig media_config_;
  std::string talk_mode_ = "free";
  bool deafened_ = false;
  int heartbeat_seconds_ = 15;
  asio::steady_timer heartbeat_timer_{signaling_transport_.context()};
  asio::steady_timer media_mode_timer_{signaling_transport_.context()};
  camera_win::CameraCaptureSession camera_;

  std::unique_ptr<webrtc_media::MediaSession> media_session_;
  std::atomic<uint64_t> mute_generation_{0};
  std::atomic<uint64_t> camera_generation_{0};
  bool camera_preview_active_ = false;
  std::atomic<bool> camera_publishing_{false};
  bool camera_toggle_pending_ = false;
  int camera_device_index_ = -1;
  std::string p2p_session_;
  std::string p2p_peer_;
  bool p2p_offerer_ = false;
  bool sfu_paused_ = false;
  bool sfu_publish_ready_ = false;
  bool sfu_subscribe_ready_ = false;
  std::string pending_sfu_request_id_;
  signaling::Json pending_sfu_room_mode_;
};

}  // namespace rtcwin::engine
