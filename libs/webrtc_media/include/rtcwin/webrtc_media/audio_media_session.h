#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace rtcwin::webrtc_media {

struct AudioMediaError {
  int code = 0;
  std::string message;
};

struct AudioIceServer {
  std::vector<std::string> urls;
  std::string username;
  std::string password;
};

struct AudioMediaConfig {
  std::string audio_backend = "wasapi";
  std::string audio_source = "mic";
  std::string recording_device;
  std::string playout_device;
  int volume = 60;
  bool muted = true;
  std::vector<AudioIceServer> ice_servers;
};

struct AudioSfuOffer {
  std::string sdp;
  std::string audio_mid;
  std::string source = "mic";
};

struct AudioMediaStatus {
  bool initialized = false;
  bool sfu_send_ready = false;
  bool sfu_recv_ready = false;
  bool p2p_ready = false;
  bool p2p_connected = false;
  bool muted = true;
  bool deafened = false;
  bool sfu_paused = false;
  bool recording_initialized = false;
  bool recording = false;
  bool playout_initialized = false;
  bool playing = false;
  bool sfu_send_active = false;
  bool p2p_send_active = false;
  double volume = 0.6;
  std::uint64_t capture_frames = 0;
  std::uint64_t remote_tracks = 0;
  std::uint64_t active_remote_tracks = 0;
  std::uint64_t sfu_audio_packets_sent = 0;
  std::uint64_t p2p_audio_packets_sent = 0;
  std::uint64_t audio_packets_received = 0;
  std::uint64_t audio_bytes_sent = 0;
  std::uint64_t audio_bytes_received = 0;
  double remote_audio_level = -1.0;
  std::string recording_device;
  std::string playout_device;
  std::string sfu_send_state;
  std::string sfu_recv_state;
  std::string p2p_state;
  std::string last_error;
};

// Audio-only media runtime. Public surface deliberately contains no libwebrtc
// types so the /MD UI side can remain isolated behind the C ABI.
class AudioMediaSession final {
 public:
  using Event = std::function<void(std::string, std::string)>;

  AudioMediaSession();
  ~AudioMediaSession();

  AudioMediaSession(const AudioMediaSession&) = delete;
  AudioMediaSession& operator=(const AudioMediaSession&) = delete;

  bool Initialize(const AudioMediaConfig& config, Event event,
                  AudioMediaError* error);
  void Shutdown();

  bool CreateSfuPublishOffer(AudioSfuOffer* offer, AudioMediaError* error);
  bool ApplySfuPublishAnswer(const std::string& sdp, AudioMediaError* error);
  bool CreateSfuSubscribeAnswer(const std::string& remote_offer,
                                std::string* answer, AudioMediaError* error);

  bool CreateP2pOffer(std::string* offer, AudioMediaError* error);
  bool CreateP2pAnswer(const std::string& remote_offer, std::string* answer,
                       AudioMediaError* error);
  bool ApplyP2pAnswer(const std::string& answer, AudioMediaError* error);
  bool AddP2pCandidate(const std::string& candidate, const std::string& mid,
                       int mline_index, AudioMediaError* error);
  void CloseP2p();

  bool SetMuted(bool muted, AudioMediaError* error);
  bool SetDeafened(bool deafened, AudioMediaError* error);
  bool SetVolume(double volume, AudioMediaError* error);
  bool SetSfuPaused(bool paused, AudioMediaError* error);

  AudioMediaStatus status() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace rtcwin::webrtc_media
