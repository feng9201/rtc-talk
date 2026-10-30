#pragma once

#include "rtcwin/muxplayer_adapter/mux_video_surface.h"

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QVector>

#include <memory>

class QWidget;

namespace rtcwin::ui {

class VideoSurfacePool;

class VideoSurfaceLease final {
 public:
  VideoSurfaceLease() = default;
  ~VideoSurfaceLease();

  VideoSurfaceLease(const VideoSurfaceLease&) = delete;
  VideoSurfaceLease& operator=(const VideoSurfaceLease&) = delete;
  VideoSurfaceLease(VideoSurfaceLease&& other) noexcept;
  VideoSurfaceLease& operator=(VideoSurfaceLease&& other) noexcept;

  muxplayer_adapter::MuxVideoSurface* surface() const {
    return surface_.data();
  }
  const QString& key() const { return key_; }
  explicit operator bool() const { return surface_ != nullptr; }

 private:
  friend class VideoSurfacePool;

  VideoSurfaceLease(VideoSurfacePool* pool, QString key,
                    muxplayer_adapter::MuxVideoSurface* surface);
  void Reset();

  QPointer<VideoSurfacePool> pool_;
  QString key_;
  QPointer<muxplayer_adapter::MuxVideoSurface> surface_;
};

class VideoSurfacePool final : public QObject {
  Q_OBJECT

 public:
  explicit VideoSurfacePool(QWidget* host, int max_surfaces = 9,
                            QObject* parent = nullptr);
  ~VideoSurfacePool() override;

  std::unique_ptr<VideoSurfaceLease> Acquire(const QString& stream_key,
                                             QWidget* owner);
  int active_count() const { return active_.size(); }
  int idle_count() const { return idle_.size(); }
  int surface_count() const { return active_.size() + idle_.size(); }
  void Shutdown();

 private:
  friend class VideoSurfaceLease;

  void Release(const QString& stream_key,
               muxplayer_adapter::MuxVideoSurface* surface);
  muxplayer_adapter::MuxVideoSurface* TakeIdle();

  QWidget* host_ = nullptr;
  int max_surfaces_ = 9;
  bool shutdown_ = false;
  QHash<QString, QPointer<muxplayer_adapter::MuxVideoSurface>> active_;
  QVector<QPointer<muxplayer_adapter::MuxVideoSurface>> idle_;
};

}  // namespace rtcwin::ui
