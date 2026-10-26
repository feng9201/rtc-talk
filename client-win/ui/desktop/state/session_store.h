#pragma once

#include "api/core_event.h"

#include <QObject>

namespace talk::desktop {

class SessionStore final : public QObject {
  Q_OBJECT

 public:
  explicit SessionStore(QObject* parent = nullptr);

  const SessionSnapshot& snapshot() const { return snapshot_; }
  void SetLocalIdentity(const QString& uid, const QString& display_name);
  void SetMediaEnabled(bool enabled);
  void SetSelectedPeer(const QString& uid);
  void SetDeafen(bool deafen);
  void Reset();
  void ApplyEvent(const CoreEvent& event);

 signals:
  void SnapshotChanged();
  void Joined();
  void Left();
  void Kicked(const QString& reason);
  void ErrorOccurred(int code, const QString& message);

 private:
  void ApplyState(const StateEvent& event);
  void ApplyReady(const ReadyEvent& event);
  void ApplyJoined(const JoinedEvent& event);
  void ApplyPeerUpdated(const PeerUpdatedEvent& event);
  void ApplyPeerJoined(const PeerJoinedEvent& event);
  void ApplyPeerLeft(const PeerLeftEvent& event);
  void ApplyVideoTrack(const VideoTrackEvent& event);
  void ApplyProducerState(const ProducerStateEvent& event);
  void ApplyFloorState(const FloorStateEvent& event);
  void ApplyFloorLocal(const FloorLocalEvent& event);
  void ApplyPolicyChanged(const PolicyChangedEvent& event);
  void ApplyAudioLevels(const AudioLevelsEvent& event);
  void ApplyMuted(const MutedEvent& event);
  void ApplyError(const ErrorEvent& event);
  void ApplyResult(const ResultEvent& event);
  void ClearRoomState();
  PeerSnapshot* FindPeer(const QString& uid);
  VideoStreamSnapshot* FindStream(const QString& mid);
  void NotifyChanged();

  SessionSnapshot snapshot_;
  qint64 last_state_update_ms_ = 0;
};

}  // namespace talk::desktop
