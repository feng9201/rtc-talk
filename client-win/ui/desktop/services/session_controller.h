#pragma once

#include "api/core_event.h"
#include "api/talk_core_client.h"
#include "domain/app_types.h"
#include "state/session_store.h"
#include "state/video_stream_registry.h"

#include <QImage>
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
  bool cameraPreviewActive() const { return camera_preview_active_; }
  const QImage& localFrame() const { return local_frame_; }
  const QVector<VideoStreamView>& videoStreams() const {
    return video_streams_.streams();
  }

 public slots:
  void PrepareLocalMedia(const PrejoinOptions& options);
  void EnumerateCameras();
  void StartCameraPreview(int camera_index);
  void StopCameraPreview();
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
  void CameraDevicesChanged(
      const QVector<talk::desktop::CameraDevice>& devices);
  void CameraPreviewChanged(bool active, const QString& device);
  void LocalVideoFrameReceived(const QImage& frame);
  void VideoStreamsChanged(
      const QVector<talk::desktop::VideoStreamView>& streams);
  void VideoFrameReady(const QString& mid, const QImage& frame);
  void VideoFrameCleared(const QString& mid);

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
  VideoStreamRegistry video_streams_;
  QNetworkAccessManager* network_ = nullptr;
  PrejoinOptions options_;
  QImage local_frame_;
  bool busy_ = false;
  bool joining_ = false;
  bool camera_preview_active_ = false;
};

}  // namespace talk::desktop
