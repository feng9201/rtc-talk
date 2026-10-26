#pragma once

#include "api/core_event.h"
#include "api/talk_core_client.h"
#include "domain/app_types.h"
#include "state/session_store.h"

#include <QObject>
#include <QString>

class QNetworkAccessManager;

namespace talk::desktop {

class SessionController final : public QObject {
  Q_OBJECT

 public:
  explicit SessionController(QObject* parent = nullptr);

  const SessionSnapshot& snapshot() const { return store_.snapshot(); }
  bool busy() const { return busy_; }

 public slots:
  void ConnectAndJoin(const PrejoinOptions& options);
  void LeaveRoom();
  void Disconnect();
  void SetMicEnabled(bool enabled);
  void ToggleCamera();
  void ToggleShare();
  void ToggleDeafen();
  void RequestFloor();
  void ReleaseFloor();
  void SelectPeer(const QString& uid);
  void GrantFloor(const QString& uid, bool allow);
  void MuteAll();
  void KickMember(const QString& uid);
  void UpdatePolicy(const RoomPolicy& policy);

 signals:
  void StateChanged(const talk::desktop::SessionSnapshot& snapshot);
  void Joined();
  void Left();
  void Kicked(const QString& reason);
  void ErrorOccurred(const QString& message, const QString& detail);
  void BusyChanged(bool busy, const QString& message);
  void Notice(const QString& message, const QString& tone);

 private:
  void HandleCoreEvent(const CoreEvent& event);
  void HandleCommandCompleted(const CommandResult& result);
  void HandleCommandTimedOut(quint64 command_id, const QString& method);
  void HandleStoreError(int code, const QString& message);
  void HandleKicked(const QString& reason);
  void SubmitConnect(const QString& token);
  void SubmitJoin();
  bool StartCore(const PrejoinOptions& options);
  void SetBusy(bool busy, const QString& message);
  void Fail(const QString& message, const QString& detail = QString());
  void ResetSession();

  TalkCoreClient core_;
  SessionStore store_;
  QNetworkAccessManager* network_ = nullptr;
  PrejoinOptions options_;
  bool busy_ = false;
  bool joining_ = false;
};

}  // namespace talk::desktop
