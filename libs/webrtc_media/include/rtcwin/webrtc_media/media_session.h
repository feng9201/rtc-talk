#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace rtcwin::webrtc_media {

struct MediaError {
  int code = 0;
  std::string message;
};

struct IceServer {
  std::vector<std::string> urls;
  std::string username;
  std::string password;
};

struct MediaConfig {
  std::string audio_backend = "wasapi";
  std::string audio_source = "mic";
  std::string recording_device;
  std::string playout_device;
  int volume = 60;
  bool muted = true;
  bool camera_publish_enabled = true;
  bool screen_publish_enabled = true;
  std::vector<IceServer> ice_servers;
};

struct VideoFrameData {
  int width = 0;
  int height = 0;
  int stride_y = 0;
  int stride_u = 0;
  int stride_v = 0;
  const std::uint8_t* data_y = nullptr;
  const std::uint8_t* data_u = nullptr;
  const std::uint8_t* data_v = nullptr;
  std::int64_t timestamp_us = 0;
};

struct RemoteVideoFrame {
  std::string mid;
  std::string uid;
  std::string source = "camera";
  int width = 0;
  int height = 0;
  int stride_y = 0;
  int stride_u = 0;
  int stride_v = 0;
  std::vector<std::uint8_t> y;
  std::vector<std::uint8_t> u;
  std::vector<std::uint8_t> v;
  std::int64_t timestamp_us = 0;
};

struct RemoteTrackInfo {
  std::string mid;
  std::string uid;
  std::string kind;
  std::string source;
};

struct MediaSfuOffer {
  std::string sdp;
  std::string audio_mid;
  std::string camera_mid;
  std::string screen_mid;
  std::string source = "mic";
};

struct MediaStatus {
  bool initialized = false;
  bool sfu_send_ready = false;
  bool sfu_recv_ready = false;
  bool p2p_ready = false;
  bool p2p_connected = false;
  bool muted = true;
  bool deafened = false;
  bool sfu_paused = false;
  bool camera_enabled = false;
  bool screen_enabled = false;
  bool recording_initialized = false;
  bool recording = false;
  bool playout_initialized = false;
  bool playing = false;
  bool sfu_send_active = false;
  bool p2p_send_active = false;
  bool camera_sender_active = false;
  bool screen_sender_active = false;
  double volume = 0.6;
  std::uint64_t capture_frames = 0;
  std::uint64_t camera_capture_frames = 0;
  std::uint64_t screen_capture_frames = 0;
  std::uint64_t remote_tracks = 0;
  std::uint64_t active_remote_tracks = 0;
  std::uint64_t remote_video_tracks = 0;
  std::uint64_t active_remote_video_tracks = 0;
  std::uint64_t sfu_audio_packets_sent = 0;
  std::uint64_t p2p_audio_packets_sent = 0;
  std::uint64_t video_packets_sent = 0;
  std::uint64_t video_packets_received = 0;
  std::uint64_t audio_packets_received = 0;
  std::uint64_t audio_bytes_sent = 0;
  std::uint64_t audio_bytes_received = 0;
  std::uint64_t video_bytes_sent = 0;
  std::uint64_t video_bytes_received = 0;
  double remote_audio_level = -1.0;
  std::string recording_device;
  std::string playout_device;
  std::string sfu_send_state;
  std::string sfu_recv_state;
  std::string p2p_state;
  std::string last_error;
};

// Audio/video media runtime. Public surface deliberately contains no libwebrtc
// types so the /MD UI side can remain isolated behind the C ABI.
class MediaSession final {
 public:
  using Event = std::function<void(std::string, std::string)>;
  using RemoteVideoCallback = std::function<void(RemoteVideoFrame)>;

  MediaSession();
  ~MediaSession();

  MediaSession(const MediaSession&) = delete;
  MediaSession& operator=(const MediaSession&) = delete;

  bool Initialize(const MediaConfig& config, Event event,
                  RemoteVideoCallback remote_video, MediaError* error);
  void Shutdown();

  bool CreateSfuPublishOffer(MediaSfuOffer* offer, MediaError* error);
  bool ApplySfuPublishAnswer(const std::string& sdp, MediaError* error);
  bool CreateSfuSubscribeAnswer(const std::string& remote_offer,
                                const std::vector<RemoteTrackInfo>& consumers,
                                std::string* answer, MediaError* error);

  bool CreateP2pOffer(bool include_video, std::string* offer,
                      MediaError* error);
  bool CreateP2pAnswer(const std::string& remote_offer, bool include_video,
                       std::string* answer, MediaError* error);
  bool ApplyP2pAnswer(const std::string& answer, MediaError* error);
  void SetP2pPeerUid(const std::string& uid);
  bool AddP2pCandidate(const std::string& candidate, const std::string& mid,
                       int mline_index, MediaError* error);
  void CloseP2p();

  bool SetMuted(bool muted, MediaError* error);
  bool SetDeafened(bool deafened, MediaError* error);
  bool SetVolume(double volume, MediaError* error);
  bool SetSfuPaused(bool paused, MediaError* error);
  bool SetCameraEnabled(bool enabled, MediaError* error);
  bool SetScreenEnabled(bool enabled, MediaError* error);
  bool PushCameraFrame(const VideoFrameData& frame, MediaError* error);
  void SetLocalScreenFrameCallback(
      std::function<void(const VideoFrameData&)> callback);

  MediaStatus status() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace rtcwin::webrtc_media
