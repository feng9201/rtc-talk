#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace rtcwin::domain {

enum class ConnectionPhase {
  Disconnected,
  Connecting,
  Ready,
  Reconnecting,
  Failed,
};

enum class JoinPhase {
  NotJoined,
  Joining,
  Joined,
  Leaving,
};

enum class RoomRole {
  Host,
  Member,
};

enum class TalkMode {
  Free,
  Ptt,
  Moderated,
};

enum class MediaModePreference {
  Auto,
  Sfu,
  P2p,
};

enum class VideoSource {
  Camera,
  Screen,
};

enum class AudioBackend {
  Wasapi,
  Fake,
  None,
};

enum class AudioSource {
  Microphone,
  System,
};

enum class PanelTab {
  Members,
  Devices,
  Status,
};

enum class StageLayout {
  Grid,
  Focus,
};

enum class StageFilter {
  All,
  VideoOnly,
};

enum class ErrorDomain {
  Validation,
  Authentication,
  Permission,
  Network,
  Protocol,
  Device,
  Media,
  Session,
  Storage,
  Internal,
};

enum class RecoveryAction {
  None,
  Retry,
  ChooseDevice,
  Reauthenticate,
  ChangeNetwork,
  RejoinRoom,
};

struct AppError {
  ErrorDomain domain = ErrorDomain::Internal;
  int code = 0;
  RecoveryAction action = RecoveryAction::None;
  bool retryable = false;
  std::string operation;
  std::string message;
  std::string detail;
};

struct CameraDevice {
  std::string id;
  std::string name;
  bool available = true;
};

struct AudioDevice {
  std::string id;
  std::string name;
  bool is_default = false;
  bool available = true;
};

struct PrejoinOptions {
  std::string signaling_url;
  std::string user_id;
  std::string room_id;
  std::string token;
  bool fetch_dev_token = false;
  AudioBackend audio_backend = AudioBackend::Wasapi;
  AudioSource audio_source = AudioSource::Microphone;
  MediaModePreference media_mode = MediaModePreference::Auto;
  TalkMode talk_mode = TalkMode::Free;
  int volume = 60;
  std::string recording_device;
  std::string playout_device;
  std::string camera_device;
};

struct PeerState {
  std::string uid;
  std::string display_name;
  RoomRole role = RoomRole::Member;
  bool audio_muted = true;
  bool speaking = false;
  bool camera_on = false;
  bool screen_on = false;
  double audio_level = 0.0;
};

struct FloorState {
  std::string holder_uid;
  std::vector<std::string> queue;
  bool self_has_floor = false;
  std::int64_t expires_in_ms = 0;
  std::int64_t deadline_epoch_ms = 0;
};

struct RoomPolicy {
  TalkMode talk_mode = TalkMode::Free;
  int max_speakers = 0;
  int ptt_max_hold_seconds = 60;
  bool ptt_queue = true;
  MediaModePreference media_mode = MediaModePreference::Auto;
  bool p2p_allow_relay = true;
  int max_peers = 20;
  bool video_enabled = true;
  int video_max_publishers = 9;
};

struct VideoStreamState {
  std::string mid;
  std::string uid;
  VideoSource source = VideoSource::Camera;
  bool producer_paused = true;
  bool receiving = false;
  std::int64_t last_frame_ms = 0;
};

struct SessionSnapshot {
  ConnectionPhase connection = ConnectionPhase::Disconnected;
  JoinPhase join = JoinPhase::NotJoined;
  std::string room_id;
  std::string self_uid;
  std::string self_display_name;
  RoomRole self_role = RoomRole::Member;
  std::string media_mode;
  std::string selected_uid;
  TalkMode talk_mode = TalkMode::Free;
  RoomPolicy policy;
  FloorState floor;
  std::vector<PeerState> peers;
  std::vector<VideoStreamState> streams;
  bool mic_muted = true;
  bool camera_on = false;
  bool sharing = false;
  bool deafened = false;
  bool media_enabled = false;
  double self_audio_level = 0.0;
  std::optional<AppError> last_error;
};

inline bool IsHost(const SessionSnapshot& state) {
  return state.self_role == RoomRole::Host;
}

inline bool CanUseRoomControls(const SessionSnapshot& state) {
  return state.join == JoinPhase::Joined &&
         state.connection == ConnectionPhase::Ready;
}

inline std::string TalkModeName(TalkMode mode) {
  switch (mode) {
    case TalkMode::Free:
      return "free";
    case TalkMode::Ptt:
      return "ptt";
    case TalkMode::Moderated:
      return "moderated";
  }
  return "free";
}

inline std::string MediaModeName(MediaModePreference mode) {
  switch (mode) {
    case MediaModePreference::Auto:
      return "auto";
    case MediaModePreference::Sfu:
      return "sfu";
    case MediaModePreference::P2p:
      return "p2p";
  }
  return "auto";
}

inline std::string VideoSourceName(VideoSource source) {
  return source == VideoSource::Screen ? "screen" : "camera";
}

}  // namespace rtcwin::domain
