#pragma once

#include "domain/app_types.h"

#include <QJsonObject>
#include <QMetaType>
#include <QString>
#include <QVector>

#include <optional>
#include <variant>

namespace talk::desktop {

struct StateEvent {
  ConnectionState state = ConnectionState::Disconnected;
};

struct ReadyEvent {
  QString uid;
  bool resumed = false;
};

struct PeerUpdatedEvent {
  QString uid;
  std::optional<QString> display_name;
  std::optional<QString> role;
  std::optional<bool> audio_muted;
  std::optional<bool> audio_speaking;
  std::optional<bool> camera_on;
  std::optional<bool> screen_on;
};

struct PeerJoinedEvent {
  PeerSnapshot peer;
};

struct PeerLeftEvent {
  QString uid;
  QString reason;
};

struct JoinedEvent {
  QString room_id;
  QString media_mode;
  QString role;
  RoomPolicy policy;
  FloorSnapshot floor;
  QVector<PeerSnapshot> peers;
};

struct LeftEvent {};

struct ModeChangedEvent {
  QString mode;
};

struct P2pStateEvent {
  QString state;
  QString path;
  QString peer;
  QString role;
};

struct VideoStateEvent {
  VideoSource source = VideoSource::Camera;
  bool sharing = false;
};

struct VideoTrackEvent {
  QString mid;
  QString uid;
  VideoSource source = VideoSource::Camera;
};

struct ProducerStateEvent {
  QString uid;
  QString mid;
  QString kind;
  VideoSource source = VideoSource::Camera;
  bool paused = true;
};

struct FloorStateEvent {
  FloorSnapshot floor;
};

struct PolicyChangedEvent {
  RoomPolicy policy;
};

struct AudioLevelEntry {
  QString uid;
  double level = 0.0;
};

struct AudioLevelsEvent {
  QVector<AudioLevelEntry> levels;
};

struct ActiveSpeakerEvent {
  QString uid;
};

struct MutedEvent {
  bool muted = true;
  QString source;
};

struct KickedEvent {
  QString reason;
};

struct ErrorEvent {
  int code = 0;
  QString message;
  QString detail;
  QString name;
};

struct ResultEvent {
  quint64 command_id = 0;
  QString method;
  bool ok = false;
  int error_code = 0;
  QString error_message;
  QJsonObject data;
};

struct UnknownEvent {
  QJsonObject data;
};

using CoreEventPayload = std::variant<
    StateEvent, ReadyEvent, PeerUpdatedEvent, PeerJoinedEvent, PeerLeftEvent,
    JoinedEvent, LeftEvent, ModeChangedEvent, P2pStateEvent, VideoStateEvent,
    VideoTrackEvent, ProducerStateEvent, FloorStateEvent, PolicyChangedEvent,
    AudioLevelsEvent, ActiveSpeakerEvent, MutedEvent, KickedEvent, ErrorEvent,
    ResultEvent, UnknownEvent>;

struct CoreEvent {
  QString name;
  CoreEventPayload payload;
};

struct CommandResult {
  quint64 command_id = 0;
  QString method;
  bool ok = false;
  int error_code = 0;
  QString error_message;
  QJsonObject data;
};

}  // namespace talk::desktop

Q_DECLARE_METATYPE(talk::desktop::CoreEvent)
Q_DECLARE_METATYPE(talk::desktop::CommandResult)
