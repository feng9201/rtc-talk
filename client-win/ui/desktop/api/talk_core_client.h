#pragma once

#include "api/command_correlator.h"
#include "api/core_event.h"

#include <QImage>
#include <QJsonObject>
#include <QObject>
#include <QString>

#include <cstdint>
#include <memory>

#include "talk/talk.h"

namespace talk::desktop {

class TalkCoreClient final : public QObject {
  Q_OBJECT

 public:
  explicit TalkCoreClient(QObject* parent = nullptr);
  ~TalkCoreClient() override;

  TalkCoreClient(const TalkCoreClient&) = delete;
  TalkCoreClient& operator=(const TalkCoreClient&) = delete;

  bool Start(const QJsonObject& config);
  void Stop();
  bool IsRunning() const { return core_ != nullptr; }

  // Returns the core operation ID, or 0 when submission fails.
  quint64 Submit(const QString& method, const QJsonObject& data = {},
                 int timeout_ms = 15000);
  void SetVideoCallback(bool enabled);
  void SetLocalVideoCallback(bool enabled);

 signals:
  void CoreEventReceived(const talk::desktop::CoreEvent& event);
  void VideoFrameReceived(const QImage& frame, const QString& mid);
  void LocalVideoFrameReceived(const QImage& frame, const QString& mid);
  void CommandCompleted(const talk::desktop::CommandResult& result);
  void CommandTimedOut(quint64 command_id, const QString& method);

 private:
  struct Context {
    TalkCoreClient* self = nullptr;
    quint64 generation = 0;
  };

  static void Dispatch(void* context, const char* name, const char* json);
  static void DispatchVideo(void* context, const char* mid,
                            const uint8_t* bgra, int width, int height,
                            int stride, int64_t timestamp_us);
  static void DispatchLocalVideo(void* context, const char* mid,
                                 const uint8_t* bgra, int width, int height,
                                 int stride, int64_t timestamp_us);
  void DeliverEvent(const QString& name, const QJsonObject& data);
  void DeliverVideo(const QImage& frame, const QString& mid);
  void DeliverLocalVideo(const QImage& frame, const QString& mid);

  talk_handle* core_ = nullptr;
  quint64 generation_ = 0;
  std::unique_ptr<Context> context_;
  CommandCorrelator correlator_;
};

}  // namespace talk::desktop
