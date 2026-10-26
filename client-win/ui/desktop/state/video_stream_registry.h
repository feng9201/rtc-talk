#pragma once

#include "api/core_event.h"

#include <QHash>
#include <QImage>
#include <QObject>
#include <QTimer>
#include <QVector>

namespace talk::desktop {

struct VideoStreamView {
  QString mid;
  QString uid;
  VideoSource source = VideoSource::Camera;
  bool paused = false;
  bool receiving = false;
  qint64 lastFrameMs = 0;
  QImage frame;
};

// 按 mid 维护远端视频元数据与最新画面。
// 所有方法只在 Qt UI 线程调用；帧刷新按 33ms 合并，避免重绘风暴。
class VideoStreamRegistry final : public QObject {
  Q_OBJECT

 public:
  explicit VideoStreamRegistry(QObject* parent = nullptr,
                               int no_frame_timeout_ms = 5000);

  const QVector<VideoStreamView>& streams() const { return streams_; }
  void ApplyEvent(const CoreEvent& event);
  void FrameReceived(const QImage& frame, const QString& mid);
  void Clear();

 signals:
  void StreamsChanged(const QVector<talk::desktop::VideoStreamView>& streams);
  void FrameReady(const QString& mid, const QImage& frame);
  void FrameCleared(const QString& mid);

 private:
  VideoStreamView* Find(const QString& mid);
  VideoStreamView& Upsert(const QString& mid);
  void FlushFrames();
  void SweepStaleFrames();
  void NotifyStreams();

  QVector<VideoStreamView> streams_;
  QHash<QString, QImage> pending_frames_;
  QHash<QString, bool> paused_by_mid_;
  QHash<QString, bool> paused_by_source_;
  QTimer frame_timer_;
  QTimer stale_timer_;
  int no_frame_timeout_ms_ = 5000;
};

}  // namespace talk::desktop

Q_DECLARE_METATYPE(talk::desktop::VideoStreamView)
Q_DECLARE_METATYPE(QVector<talk::desktop::VideoStreamView>)
