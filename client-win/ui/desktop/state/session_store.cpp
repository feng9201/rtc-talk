#include "state/session_store.h"

#include <QDateTime>

#include <utility>

namespace talk::desktop {
namespace {

QString MediaModeFromPreference(MediaModePreference mode) {
  switch (mode) {
    case MediaModePreference::Sfu:
      return QStringLiteral("sfu");
    case MediaModePreference::P2p:
      return QStringLiteral("p2p");
    case MediaModePreference::Auto:
      return QStringLiteral("auto");
  }
  return QStringLiteral("auto");
}

}  // namespace

SessionStore::SessionStore(QObject* parent) : QObject(parent) {
  qRegisterMetaType<SessionSnapshot>();
}

void SessionStore::SetLocalIdentity(const QString& uid,
                                    const QString& display_name) {
  snapshot_.selfUid = uid;
  snapshot_.selfDisplayName = display_name;
  NotifyChanged();
}

void SessionStore::Reset() {
  const bool was_joined = snapshot_.joined;
  snapshot_ = SessionSnapshot{};
  NotifyChanged();
  if (was_joined) emit Left();
}

void SessionStore::ApplyEvent(const CoreEvent& event) {
  if (const auto* state = std::get_if<StateEvent>(&event.payload)) {
    ApplyState(*state);
  } else if (const auto* ready = std::get_if<ReadyEvent>(&event.payload)) {
    ApplyReady(*ready);
  } else if (const auto* joined = std::get_if<JoinedEvent>(&event.payload)) {
    ApplyJoined(*joined);
  } else if (const auto* update =
                 std::get_if<PeerUpdatedEvent>(&event.payload)) {
    ApplyPeerUpdated(*update);
  } else if (const auto* joined_peer =
                 std::get_if<PeerJoinedEvent>(&event.payload)) {
    ApplyPeerJoined(*joined_peer);
  } else if (const auto* left =
                 std::get_if<PeerLeftEvent>(&event.payload)) {
    ApplyPeerLeft(*left);
  } else if (std::get_if<LeftEvent>(&event.payload) != nullptr) {
    const bool was_joined = snapshot_.joined;
    ClearRoomState();
    snapshot_.connectionState = ConnectionState::Ready;
    NotifyChanged();
    if (was_joined) emit Left();
  } else if (const auto* mode =
                 std::get_if<ModeChangedEvent>(&event.payload)) {
    snapshot_.mediaMode = mode->mode;
    NotifyChanged();
  } else if (const auto* video =
                 std::get_if<VideoStateEvent>(&event.payload)) {
    if (video->source == VideoSource::Camera) {
      snapshot_.cameraOn = video->sharing;
    } else {
      snapshot_.sharing = video->sharing;
    }
    NotifyChanged();
  } else if (const auto* track =
                 std::get_if<VideoTrackEvent>(&event.payload)) {
    ApplyVideoTrack(*track);
  } else if (const auto* producer =
                 std::get_if<ProducerStateEvent>(&event.payload)) {
    ApplyProducerState(*producer);
  } else if (const auto* floor =
                 std::get_if<FloorStateEvent>(&event.payload)) {
    ApplyFloorState(*floor);
  } else if (const auto* policy =
                 std::get_if<PolicyChangedEvent>(&event.payload)) {
    ApplyPolicyChanged(*policy);
  } else if (const auto* levels =
                 std::get_if<AudioLevelsEvent>(&event.payload)) {
    ApplyAudioLevels(*levels);
  } else if (const auto* speaker =
                 std::get_if<ActiveSpeakerEvent>(&event.payload)) {
    snapshot_.activeSpeakerUid = speaker->uid;
    NotifyChanged();
  } else if (const auto* muted =
                 std::get_if<MutedEvent>(&event.payload)) {
    ApplyMuted(*muted);
  } else if (const auto* kicked =
                 std::get_if<KickedEvent>(&event.payload)) {
    ClearRoomState();
    snapshot_.connectionState = ConnectionState::Failed;
    snapshot_.lastError = kicked->reason == QStringLiteral("replaced")
                              ? QStringLiteral("账号已在其他客户端登录")
                              : QStringLiteral("已被移出房间");
    NotifyChanged();
    emit Kicked(kicked->reason);
  } else if (const auto* error =
                 std::get_if<ErrorEvent>(&event.payload)) {
    ApplyError(*error);
  } else if (const auto* result =
                 std::get_if<ResultEvent>(&event.payload)) {
    ApplyResult(*result);
  }
}

void SessionStore::ApplyState(const StateEvent& event) {
  const bool was_joined = snapshot_.joined;
  snapshot_.connectionState = event.state;
  if (event.state == ConnectionState::Disconnected) {
    ClearRoomState();
  }
  NotifyChanged();
  if (was_joined && event.state == ConnectionState::Disconnected) emit Left();
}

void SessionStore::ApplyReady(const ReadyEvent& event) {
  snapshot_.connectionState = ConnectionState::Ready;
  if (!event.uid.isEmpty()) snapshot_.selfUid = event.uid;
  snapshot_.lastError.clear();
  snapshot_.lastErrorCode = 0;
  NotifyChanged();
}

void SessionStore::ApplyJoined(const JoinedEvent& event) {
  ClearRoomState();
  snapshot_.joined = true;
  snapshot_.connectionState = ConnectionState::Ready;
  snapshot_.roomId = event.room_id;
  snapshot_.mediaMode = event.media_mode;
  snapshot_.selfRole = event.role;
  snapshot_.policy = event.policy;
  snapshot_.talkMode = event.policy.talkMode;
  snapshot_.floor = event.floor;
  if (snapshot_.floor.expiresInMs > 0) {
    snapshot_.floor.deadlineEpochMs =
        QDateTime::currentMSecsSinceEpoch() + snapshot_.floor.expiresInMs;
  }
  snapshot_.peers = event.peers;
  NotifyChanged();
  emit Joined();
}

void SessionStore::ApplyPeerUpdated(const PeerUpdatedEvent& event) {
  if (event.uid == snapshot_.selfUid) {
    if (event.display_name.has_value()) {
      snapshot_.selfDisplayName = *event.display_name;
    }
    if (event.role.has_value()) snapshot_.selfRole = *event.role;
    NotifyChanged();
    return;
  }
  PeerSnapshot* peer = FindPeer(event.uid);
  if (peer == nullptr) return;
  if (event.display_name.has_value()) peer->displayName = *event.display_name;
  if (event.role.has_value()) peer->role = *event.role;
  if (event.audio_muted.has_value()) peer->audioMuted = *event.audio_muted;
  if (event.camera_on.has_value()) peer->cameraOn = *event.camera_on;
  if (event.screen_on.has_value()) peer->screenOn = *event.screen_on;
  NotifyChanged();
}

void SessionStore::ApplyPeerJoined(const PeerJoinedEvent& event) {
  if (FindPeer(event.peer.uid) == nullptr) {
    snapshot_.peers.append(event.peer);
  }
  NotifyChanged();
}

void SessionStore::ApplyPeerLeft(const PeerLeftEvent& event) {
  for (int i = 0; i < snapshot_.peers.size(); ++i) {
    if (snapshot_.peers.at(i).uid == event.uid) {
      snapshot_.peers.removeAt(i);
      break;
    }
  }
  if (snapshot_.activeSpeakerUid == event.uid) {
    snapshot_.activeSpeakerUid.clear();
  }
  NotifyChanged();
}

void SessionStore::ApplyVideoTrack(const VideoTrackEvent& event) {
  if (event.mid.isEmpty()) return;
  VideoStreamSnapshot* stream = FindStream(event.mid);
  if (stream == nullptr) {
    snapshot_.streams.append(VideoStreamSnapshot{event.mid, event.uid,
                                                 event.source, true});
  } else {
    stream->uid = event.uid;
    stream->source = event.source;
  }
  NotifyChanged();
}

void SessionStore::ApplyProducerState(const ProducerStateEvent& event) {
  if (event.kind != QStringLiteral("video")) return;
  for (VideoStreamSnapshot& stream : snapshot_.streams) {
    if (stream.uid == event.uid && stream.source == event.source) {
      stream.paused = event.paused;
    }
  }
  NotifyChanged();
}

void SessionStore::ApplyFloorState(const FloorStateEvent& event) {
  snapshot_.floor = event.floor;
  if (snapshot_.floor.expiresInMs > 0) {
    snapshot_.floor.deadlineEpochMs =
        QDateTime::currentMSecsSinceEpoch() + snapshot_.floor.expiresInMs;
  }
  NotifyChanged();
}

void SessionStore::ApplyPolicyChanged(const PolicyChangedEvent& event) {
  snapshot_.policy = event.policy;
  snapshot_.talkMode = event.policy.talkMode;
  if (event.policy.mediaMode != MediaModePreference::Auto) {
    snapshot_.mediaMode = MediaModeFromPreference(event.policy.mediaMode);
  }
  NotifyChanged();
}

void SessionStore::ApplyAudioLevels(const AudioLevelsEvent& event) {
  for (PeerSnapshot& peer : snapshot_.peers) peer.audioLevel = 0.0;
  for (const AudioLevelEntry& entry : event.levels) {
    if (PeerSnapshot* peer = FindPeer(entry.uid)) {
      peer->audioLevel = entry.level;
    }
  }
  NotifyChanged();
}

void SessionStore::ApplyMuted(const MutedEvent& event) {
  if (event.source.isEmpty() || event.source == QStringLiteral("mic")) {
    snapshot_.micMuted = event.muted;
  }
  NotifyChanged();
}

void SessionStore::ApplyError(const ErrorEvent& event) {
  snapshot_.lastErrorCode = event.code;
  snapshot_.lastError = event.detail.isEmpty()
                            ? event.message
                            : QStringLiteral("%1：%2").arg(event.message,
                                                           event.detail);
  NotifyChanged();
  emit ErrorOccurred(event.code, snapshot_.lastError);
}

void SessionStore::ApplyResult(const ResultEvent& event) {
  if (event.ok) return;
  snapshot_.lastErrorCode = event.error_code;
  snapshot_.lastError = event.error_message;
  NotifyChanged();
  emit ErrorOccurred(event.error_code, event.error_message);
}

void SessionStore::ClearRoomState() {
  snapshot_.joined = false;
  snapshot_.roomId.clear();
  snapshot_.selfRole.clear();
  snapshot_.mediaMode.clear();
  snapshot_.activeSpeakerUid.clear();
  snapshot_.talkMode = TalkMode::Free;
  snapshot_.policy = RoomPolicy{};
  snapshot_.floor = FloorSnapshot{};
  snapshot_.peers.clear();
  snapshot_.streams.clear();
  snapshot_.micMuted = true;
  snapshot_.cameraOn = false;
  snapshot_.sharing = false;
  snapshot_.deafen = false;
}

PeerSnapshot* SessionStore::FindPeer(const QString& uid) {
  for (PeerSnapshot& peer : snapshot_.peers) {
    if (peer.uid == uid) return &peer;
  }
  return nullptr;
}

VideoStreamSnapshot* SessionStore::FindStream(const QString& mid) {
  for (VideoStreamSnapshot& stream : snapshot_.streams) {
    if (stream.mid == mid) return &stream;
  }
  return nullptr;
}

void SessionStore::NotifyChanged() {
  last_state_update_ms_ = QDateTime::currentMSecsSinceEpoch();
  emit SnapshotChanged();
}

}  // namespace talk::desktop
