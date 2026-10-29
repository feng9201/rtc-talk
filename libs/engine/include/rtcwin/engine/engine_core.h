#pragma once

#include "rtcwin/camera_win/camera_capture.h"
#include "rtcwin/client_api.h"
#include "rtcwin/signaling/signaling_client.h"
#include "rtcwin/webrtc_media/audio_media_session.h"

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
    std::string json;
    std::function<void()> task;
  };

  void PostToWorker(std::function<void()> task);
  void PostToSignaling(std::function<void()> task);
  void WorkerLoop();
  void HandleCommand(const Command& command);
  void HandleCameraDevices(const Command& command);
  void HandleCameraPreviewStart(const Command& command);
  void HandleCameraPreviewStop(const Command& command);
  void HandleAudioDevices(const Command& command);
  void HandleSessionConnectAndJoin(const Command& command);
  void HandleMediaSetMute(const Command& command);
  void HandleMediaSetVolume(const Command& command);
  void HandleMediaSetDeafened(const Command& command);
  void HandleMediaStats(const Command& command);
  void HandleRoomLeave(const Command& command);
  void HandleSessionDisconnect(const Command& command);
  void ScheduleHeartbeat(int seconds);
  void StopSignaling();
  void OnCameraFrame(const camera_win::I420Frame& frame);

  void FinalizeJoin(uint64_t command_id, const signaling::Json& data);
  bool InitializeAudioMedia(const signaling::Json& join_data,
                            std::string* error);
  void StartMediaForCurrentMode();
  void PublishSfu();
  void ApplySfuAnswer(const std::string& sdp);
  void StartP2p();
  void ApplyP2pOffer(const std::string& session, const std::string& from,
                     const std::string& sdp);
  void ApplyP2pAnswer(const std::string& session, const std::string& sdp);
  void ApplyP2pCandidate(const std::string& session,
                         const std::string& candidate,
                         const std::string& mid, int index);
  void HandleMediaModeRequest(const std::string& request_id,
                              const signaling::Json& data);
  void HandleSfuOfferRequest(const std::string& request_id,
                             const signaling::Json& data);
  void TryCompleteSfuSwitch(bool timed_out);
  void FailPendingSfuSwitch(int code, const std::string& message);
  void ScheduleSfuSwitchTimeout(const std::string& request_id);
  void HandleAudioMediaEvent(const std::string& method,
                             const std::string& data_json);
  void ResetAudioMedia();
  void EmitMediaError(int code, const std::string& message,
                      const std::string& detail);

  void EmitResponse(uint64_t id, const std::string& method, bool ok,
                    int error_code, const std::string& message,
                    const std::string& data_json = "{}");
  void EmitNotification(const std::string& method,
                        const std::string& data_json);
  static int ParseIntField(const std::string& json, const std::string& name,
                           int fallback);

  rtcwin_options options_{};
  std::string config_json_;
  std::atomic<uint64_t> next_id_{1};
  std::thread worker_;
  std::mutex mutex_;
  std::condition_variable cv_;
  std::deque<Command> commands_;
  bool stopping_ = false;
  asio::io_context signaling_io_;
  std::unique_ptr<asio::executor_work_guard<asio::io_context::executor_type>>
      signaling_work_;
  std::thread signaling_thread_;
  std::unique_ptr<signaling::SignalingClient> signaling_;
  std::atomic<uint64_t> session_command_id_{0};
  std::mutex session_mutex_;
  std::string session_url_;
  std::string session_token_;
  std::string session_uid_;
  std::string session_room_id_;
  std::string session_display_;
  webrtc_media::AudioMediaConfig media_config_;
  std::string media_mode_ = "sfu";
  std::string talk_mode_ = "free";
  bool session_ready_ = false;
  bool room_joined_ = false;
  bool media_enabled_ = false;
  bool deafened_ = false;
  int heartbeat_seconds_ = 15;
  asio::steady_timer heartbeat_timer_{signaling_io_};
  asio::steady_timer media_mode_timer_{signaling_io_};
  camera_win::CameraCaptureSession camera_;

  std::unique_ptr<webrtc_media::AudioMediaSession> audio_media_;
  std::atomic<uint64_t> mute_generation_{0};
  std::string p2p_session_;
  std::string p2p_peer_;
  bool p2p_offerer_ = false;
  bool sfu_paused_ = false;
  bool sfu_publish_ready_ = false;
  bool sfu_subscribe_ready_ = false;
  std::string pending_sfu_request_id_;
  std::string pending_sfu_room_mode_json_;
};

}  // namespace rtcwin::engine
