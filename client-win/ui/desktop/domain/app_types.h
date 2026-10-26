#pragma once

#include <QMetaType>
#include <QString>
#include <QStringList>
#include <QVector>

namespace talk::desktop {

enum class ConnectionState {
  Disconnected,
  Connecting,
  Ready,
  Reconnecting,
  Failed,
};

enum class MediaModePreference {
  Auto,
  Sfu,
  P2p,
};

enum class AudioBackend {
  None,
  Fake,
  Wasapi,
};

enum class PanelTab {
  Members,
  Devices,
  Status,
};

enum class AudioSource {
  Microphone,
  System,
};

enum class TalkMode {
  Free,
  Ptt,
  Moderated,
};

enum class VideoSource {
  Camera,
  Screen,
};

struct PrejoinOptions {
  QString url;
  QString userId;
  QString roomId;
  QString token;
  bool fetchDevToken = false;
  AudioBackend audioBackend = AudioBackend::Wasapi;
  MediaModePreference mediaMode = MediaModePreference::Auto;
  TalkMode talkMode = TalkMode::Free;
  AudioSource audioSource = AudioSource::Microphone;
  int volume = 60;
  QString recordingDevice;
  QString playoutDevice;
  QString cameraDevice;
};

struct PeerSnapshot {
  QString uid;
  QString displayName;
  QString role;
  bool audioMuted = true;
  bool cameraOn = false;
  bool screenOn = false;
  double audioLevel = 0.0;
};

struct VideoStreamSnapshot {
  QString mid;
  QString uid;
  VideoSource source = VideoSource::Camera;
  bool paused = true;
};

struct FloorSnapshot {
  QString holderUid;
  QStringList queue;
  bool selfHasFloor = false;
  qint64 deadlineEpochMs = 0;
};

struct RoomPolicy {
  TalkMode talkMode = TalkMode::Free;
  int maxSpeakers = 0;
  int pttMaxHoldSeconds = 60;
  bool pttQueue = true;
  MediaModePreference mediaMode = MediaModePreference::Auto;
  bool p2pAllowRelay = true;
  int maxPeers = 20;
  bool videoEnabled = true;
  int videoMaxPublishers = 9;
};

struct SessionSnapshot {
  ConnectionState connectionState = ConnectionState::Disconnected;
  bool joined = false;
  QString roomId;
  QString selfUid;
  QString selfDisplayName;
  QString selfRole;
  QString mediaMode;
  QString activeSpeakerUid;
  TalkMode talkMode = TalkMode::Free;
  RoomPolicy policy;
  FloorSnapshot floor;
  QVector<PeerSnapshot> peers;
  QVector<VideoStreamSnapshot> streams;
  bool micMuted = true;
  bool cameraOn = false;
  bool sharing = false;
  bool deafen = false;
};

}  // namespace talk::desktop

Q_DECLARE_METATYPE(talk::desktop::PrejoinOptions)
Q_DECLARE_METATYPE(talk::desktop::SessionSnapshot)
Q_DECLARE_METATYPE(talk::desktop::PanelTab)
