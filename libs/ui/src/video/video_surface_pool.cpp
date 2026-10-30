#include "rtcwin/ui/video/video_surface_pool.h"

#include <QVariant>
#include <QWidget>

#include <algorithm>

namespace rtcwin::ui {

VideoSurfaceLease::VideoSurfaceLease(
    VideoSurfacePool* pool, QString key,
    muxplayer_adapter::MuxVideoSurface* surface)
    : pool_(pool), key_(std::move(key)), surface_(surface) {}

VideoSurfaceLease::~VideoSurfaceLease() { Reset(); }

VideoSurfaceLease::VideoSurfaceLease(VideoSurfaceLease&& other) noexcept
    : pool_(other.pool_),
      key_(std::move(other.key_)),
      surface_(other.surface_) {
  other.pool_ = nullptr;
  other.surface_ = nullptr;
  other.key_.clear();
}

VideoSurfaceLease& VideoSurfaceLease::operator=(
    VideoSurfaceLease&& other) noexcept {
  if (this == &other) return *this;
  Reset();
  pool_ = other.pool_;
  key_ = std::move(other.key_);
  surface_ = other.surface_;
  other.pool_ = nullptr;
  other.surface_ = nullptr;
  other.key_.clear();
  return *this;
}

void VideoSurfaceLease::Reset() {
  if (pool_ != nullptr && !key_.isEmpty()) {
    pool_->Release(key_, surface_);
  }
  pool_ = nullptr;
  surface_ = nullptr;
  key_.clear();
}

VideoSurfacePool::VideoSurfacePool(QWidget* host, int max_surfaces,
                                   QObject* parent)
    : QObject(parent),
      host_(host),
      max_surfaces_(std::max(1, max_surfaces)) {}

VideoSurfacePool::~VideoSurfacePool() { Shutdown(); }

std::unique_ptr<VideoSurfaceLease> VideoSurfacePool::Acquire(
    const QString& stream_key, QWidget* owner) {
  if (shutdown_ || host_ == nullptr || owner == nullptr ||
      stream_key.isEmpty() || active_.contains(stream_key)) {
    return nullptr;
  }

  muxplayer_adapter::MuxVideoSurface* surface = TakeIdle();
  if (surface == nullptr) {
    if (surface_count() >= max_surfaces_) return nullptr;
    surface = new muxplayer_adapter::MuxVideoSurface(host_);
    surface->setObjectName(QStringLiteral("PooledVideoSurface"));
    surface->hide();
  }

  surface->Clear();
  surface->setProperty("streamKey", stream_key);
  surface->setParent(owner);
  surface->setAttribute(Qt::WA_TransparentForMouseEvents, true);
  surface->show();
  active_.insert(stream_key, surface);
  return std::unique_ptr<VideoSurfaceLease>(
      new VideoSurfaceLease(this, stream_key, surface));
}

void VideoSurfacePool::Shutdown() {
  if (shutdown_) return;
  shutdown_ = true;
  for (auto& surface : active_) {
    if (surface != nullptr) surface->hide();
  }
  active_.clear();
  for (auto& surface : idle_) {
    if (surface != nullptr) surface->deleteLater();
  }
  idle_.clear();
}

void VideoSurfacePool::Release(
    const QString& stream_key,
    muxplayer_adapter::MuxVideoSurface* surface) {
  const auto it = active_.find(stream_key);
  if (it == active_.end()) return;
  muxplayer_adapter::MuxVideoSurface* active_surface = it.value();
  active_.erase(it);
  if (surface != nullptr && active_surface != surface) return;
  surface = active_surface;
  if (surface == nullptr) return;

  surface->Clear();
  surface->hide();
  if (!shutdown_ && host_ != nullptr) {
    surface->setParent(host_);
    idle_.push_back(surface);
  } else {
    surface->deleteLater();
  }
}

muxplayer_adapter::MuxVideoSurface* VideoSurfacePool::TakeIdle() {
  while (!idle_.isEmpty()) {
    const QPointer<muxplayer_adapter::MuxVideoSurface> surface =
        idle_.takeLast();
    if (surface != nullptr) return surface;
  }
  return nullptr;
}

}  // namespace rtcwin::ui
