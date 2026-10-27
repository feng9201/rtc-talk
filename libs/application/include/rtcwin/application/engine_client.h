#pragma once

#include "rtcwin/domain/types.h"

#include <QObject>

namespace rtcwin::application {

class EngineClient : public QObject {
  Q_OBJECT

 public:
  explicit EngineClient(QObject* parent = nullptr) : QObject(parent) {}
  ~EngineClient() override = default;

  EngineClient(const EngineClient&) = delete;
  EngineClient& operator=(const EngineClient&) = delete;

  virtual void ConnectAndJoin(const domain::PrejoinOptions& options) = 0;
  virtual void LeaveRoom() = 0;
  virtual void Disconnect() = 0;
  virtual void SetMicEnabled(bool enabled) = 0;
  virtual void ToggleCamera() = 0;
  virtual void ToggleShare() = 0;
  virtual void ToggleDeafened() = 0;
  virtual void RequestFloor() = 0;
  virtual void ReleaseFloor() = 0;
  virtual void SelectPeer(const std::string& uid) = 0;

 signals:
  void SnapshotChanged(const rtcwin::domain::SessionSnapshot& snapshot);
  void Joined();
  void Left();
  void Notice(const QString& message, const QString& tone);
  void ErrorOccurred(const rtcwin::domain::AppError& error);
};

}  // namespace rtcwin::application
