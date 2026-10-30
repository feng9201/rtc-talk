#include "rtcwin/ui/video/video_grid.h"

#include <QDateTime>
#include <QGridLayout>
#include <QPainter>
#include <QPainterPath>
#include <QResizeEvent>
#include <QSet>
#include <QTimer>

#include <utility>
#include <vector>

namespace rtcwin::ui {
namespace {

constexpr int kHealthTickMs = 500;
constexpr int kMaxVideoSurfaces = 9;

QString FrameKey(const std::string& uid, domain::VideoSource source) {
  return QString::fromStdString(domain::VideoStreamKeyName({uid, source}));
}

std::int64_t NowMs() { return QDateTime::currentMSecsSinceEpoch(); }

struct SurfaceBinding {
  QString key;
  VideoTile* owner = nullptr;
  bool pip = false;
};

}  // namespace

VideoGrid::VideoGrid(QWidget* parent) : QWidget(parent) {
  setMinimumSize(360, 240);
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  layout_engine_ = new QGridLayout(this);
  layout_engine_->setContentsMargins(0, 0, 0, 0);
  layout_engine_->setSpacing(10);

  surface_pool_ = std::make_unique<VideoSurfacePool>(
      this, kMaxVideoSurfaces, nullptr);
  health_timer_ = new QTimer(this);
  health_timer_->setInterval(kHealthTickMs);
  connect(health_timer_, &QTimer::timeout, this,
          &VideoGrid::TickFrameHealth);
  health_timer_->start();
}

VideoGrid::~VideoGrid() {
  ClearSurfaceLeases();
  if (surface_pool_ != nullptr) surface_pool_->Shutdown();
  surface_pool_.reset();
}

void VideoGrid::SetState(const domain::SessionSnapshot& snapshot) {
  snapshot_ = snapshot;
  health_monitor_.Sync(snapshot_);
  Rebuild();
  ApplyPlacement();
  update();
}

void VideoGrid::SetFrame(const domain::VideoFrame& frame) {
  QString uid = QString::fromStdString(frame.uid);
  if (uid.isEmpty()) uid = QString::fromStdString(snapshot_.self_uid);
  if (uid.isEmpty()) return;
  domain::VideoFrame copy = frame;
  copy.uid = uid.toStdString();
  const domain::VideoStreamKey key{copy.uid, copy.source};
  health_monitor_.NoteFrame(key, copy.mid, NowMs());
  auto stored = std::make_shared<domain::VideoFrame>(std::move(copy));
  cleared_stale_streams_.remove(FrameKey(stored->uid, stored->source));
  frames_.insert(FrameKey(stored->uid, stored->source), stored);
  if (surface_leases_.find(FrameKey(stored->uid, stored->source)) ==
      surface_leases_.end()) {
    BindSurfaces();
  }
  ApplyFrameToMatchingTiles(*stored);
}

VideoGridDiagnostics VideoGrid::Diagnostics() const {
  return VideoGridDiagnostics{
      static_cast<int>(tiles_.size()),
      surface_pool_ != nullptr ? surface_pool_->active_count() : 0,
      surface_pool_ != nullptr ? surface_pool_->idle_count() : 0,
      static_cast<int>(frames_.size()),
      static_cast<int>(health_monitor_.StaleStreams(NowMs()).size())};
}

void VideoGrid::SetLayoutMode(domain::StageLayout layout) {
  layout_ = layout;
  Rebuild();
  ApplyPlacement();
  update();
}

void VideoGrid::SetFilter(domain::StageFilter filter) {
  filter_ = filter;
  Rebuild();
  ApplyPlacement();
  update();
}

domain::PeerState VideoGrid::MemberForUid(const std::string& uid) const {
  domain::PeerState member;
  member.uid = uid;
  if (uid == snapshot_.self_uid) {
    member.display_name = snapshot_.self_display_name;
    member.role = snapshot_.self_role;
    member.audio_muted = snapshot_.mic_muted;
    member.speaking = snapshot_.floor.self_has_floor;
    member.camera_on = snapshot_.camera_on;
    member.screen_on = snapshot_.sharing;
    member.audio_level = snapshot_.self_audio_level;
    return member;
  }
  for (const auto& peer : snapshot_.peers) {
    if (peer.uid == uid) return peer;
  }
  return member;
}

void VideoGrid::Rebuild() {
  scene_ = application::VideoSceneBuilder::Build(snapshot_, layout_, filter_);
  layout_plan_ = VideoLayoutEngine::Build(scene_);
  QSet<QString> active_keys;
  std::vector<domain::VideoStreamKey> active_streams;
  const auto apply_item =
      [this, &active_keys, &active_streams](
          const application::VideoSceneItem& item, bool compact) {
        VideoTile* tile = EnsureTile(item);
        const QString key = TileKey(item);
        active_keys.insert(key);
        active_streams.push_back(StreamKey(item));
        tile->SetMember(MemberForUid(item.uid), item.self, item.selected,
                        compact);
        tile->SetSource(item.source, item.has_camera_pip);
        if (item.has_camera_pip) {
          active_streams.push_back(
              domain::VideoStreamKey{item.uid, domain::VideoSource::Camera});
          active_keys.insert(
              FrameKey(item.uid, domain::VideoSource::Camera));
        }
      };

  const bool compact_primary =
      scene_.mode != application::VideoSceneMode::Grid;
  for (const auto& item : scene_.primary) {
    apply_item(item, compact_primary);
  }
  for (const auto& item : scene_.secondary) {
    apply_item(item, true);
  }

  health_monitor_.RemoveExcept(active_streams);
  for (auto it = cleared_stale_streams_.begin();
       it != cleared_stale_streams_.end();) {
    if (!active_keys.contains(*it)) {
      it = cleared_stale_streams_.erase(it);
    } else {
      ++it;
    }
  }

  for (auto it = tiles_.begin(); it != tiles_.end();) {
    if (!active_keys.contains(it.key())) {
      it.value()->DetachSurfaces();
      it.value()->deleteLater();
      it = tiles_.erase(it);
    } else {
      ++it;
    }
  }
  for (auto it = frames_.begin(); it != frames_.end();) {
    if (!active_keys.contains(it.key())) {
      it = frames_.erase(it);
    } else {
      ++it;
    }
  }

  BindSurfaces();
  for (const auto& item : scene_.primary) {
    ApplyFrame(tiles_.value(TileKey(item), nullptr), item);
  }
  for (const auto& item : scene_.secondary) {
    ApplyFrame(tiles_.value(TileKey(item), nullptr), item);
  }
}

void VideoGrid::ApplyFrame(
    VideoTile* tile, const application::VideoSceneItem& item) {
  if (tile == nullptr) return;
  if (item.source == domain::VideoSource::Screen) {
    const domain::VideoStreamKey screen_key{
        item.uid, domain::VideoSource::Screen};
    if (IsSurfaceRenderable(screen_key)) {
      if (auto frame = frames_.value(FrameKey(item.uid, screen_key.source),
                                     nullptr)) {
        tile->SetFrame(*frame);
      } else {
        tile->ClearFrame();
      }
    } else {
      frames_.remove(FrameKey(item.uid, screen_key.source));
      tile->ClearFrame();
    }

    if (item.has_camera_pip) {
      const domain::VideoStreamKey camera_key{
          item.uid, domain::VideoSource::Camera};
      if (IsSurfaceRenderable(camera_key)) {
        if (auto camera = frames_.value(
                FrameKey(item.uid, camera_key.source), nullptr)) {
          tile->SetPipFrame(*camera);
        } else {
          tile->ClearPipFrame();
        }
      } else {
        frames_.remove(FrameKey(item.uid, camera_key.source));
        tile->ClearPipFrame();
      }
    } else {
      tile->ClearPipFrame();
    }
    return;
  }

  const domain::VideoStreamKey camera_key{
      item.uid, domain::VideoSource::Camera};
  if (!IsSurfaceRenderable(camera_key)) {
    frames_.remove(FrameKey(item.uid, camera_key.source));
    tile->ClearFrame();
    return;
  }
  tile->ClearPipFrame();
  if (auto frame = frames_.value(FrameKey(item.uid, camera_key.source),
                                 nullptr)) {
    tile->SetFrame(*frame);
  } else {
    tile->ClearFrame();
  }
}

void VideoGrid::ApplyFrameToMatchingTiles(
    const domain::VideoFrame& frame) {
  const domain::VideoStreamKey frame_key{frame.uid, frame.source};
  const bool renderable = IsSurfaceRenderable(frame_key);
  const auto apply_item =
      [this, &frame, &frame_key, renderable](
          const application::VideoSceneItem& item) {
        const bool primary = item.source == frame.source;
        const bool camera_pip =
            frame.source == domain::VideoSource::Camera &&
            item.source == domain::VideoSource::Screen &&
            item.has_camera_pip;
        if (!primary && !camera_pip) return;
        VideoTile* tile = tiles_.value(TileKey(item), nullptr);
        if (tile == nullptr) return;
        if (primary) {
          if (renderable) {
            tile->SetFrame(frame);
          } else {
            frames_.remove(FrameKey(frame_key.uid, frame_key.source));
            tile->ClearFrame();
          }
        }
        if (camera_pip) {
          if (renderable) {
            tile->SetPipFrame(frame);
          } else {
            frames_.remove(FrameKey(frame_key.uid, frame_key.source));
            tile->ClearPipFrame();
          }
        }
      };
  for (const auto& item : scene_.primary) apply_item(item);
  for (const auto& item : scene_.secondary) apply_item(item);
}

void VideoGrid::BindSurfaces() {
  std::vector<SurfaceBinding> bindings;
  QSet<QString> desired_keys;

  const auto add_binding =
      [this, &bindings, &desired_keys](
          const QString& key, VideoTile* owner, bool pip,
          const domain::VideoStreamKey& stream_key) {
        if (owner == nullptr || desired_keys.contains(key) ||
            !IsSurfaceRenderable(stream_key)) {
          return;
        }
        desired_keys.insert(key);
        bindings.push_back(SurfaceBinding{key, owner, pip});
      };

  const auto collect = [this, &add_binding](
                           const application::VideoSceneItem& item) {
    VideoTile* tile = tiles_.value(TileKey(item), nullptr);
    add_binding(TileKey(item), tile, false, StreamKey(item));
    if (item.has_camera_pip) {
      add_binding(
          FrameKey(item.uid, domain::VideoSource::Camera), tile, true,
          domain::VideoStreamKey{item.uid, domain::VideoSource::Camera});
    }
  };
  for (const auto& item : scene_.primary) collect(item);
  for (const auto& item : scene_.secondary) collect(item);

  for (auto it = tiles_.begin(); it != tiles_.end(); ++it) {
    it.value()->DetachSurfaces();
  }
  for (auto it = surface_leases_.begin(); it != surface_leases_.end();) {
    if (!desired_keys.contains(it->first)) {
      it = surface_leases_.erase(it);
    } else {
      ++it;
    }
  }

  for (const auto& binding : bindings) {
    auto lease = surface_leases_.find(binding.key);
    if (lease == surface_leases_.end()) {
      auto acquired = surface_pool_->Acquire(binding.key, binding.owner);
      if (!acquired) continue;
      lease = surface_leases_.emplace(
          binding.key, std::move(acquired)).first;
    }
    auto* surface = lease->second->surface();
    if (surface == nullptr) {
      surface_leases_.erase(lease);
      continue;
    }
    surface->setParent(binding.owner);
    if (binding.pip) {
      binding.owner->AttachPipSurface(surface);
    } else {
      binding.owner->AttachMainSurface(surface);
    }
  }
}

void VideoGrid::ClearSurfaceLeases() {
  for (auto it = tiles_.begin(); it != tiles_.end(); ++it) {
    it.value()->DetachSurfaces();
  }
  surface_leases_.clear();
}

void VideoGrid::TickFrameHealth() {
  const auto stale = health_monitor_.StaleStreams(NowMs());
  bool cleared = false;
  for (const auto& key : stale) {
    const QString name = FrameKey(key.uid, key.source);
    if (cleared_stale_streams_.contains(name)) continue;
    cleared_stale_streams_.insert(name);
    ClearStreamFrames(key);
    cleared = true;
  }
  if (cleared) update();
}

void VideoGrid::ClearStreamFrames(const domain::VideoStreamKey& key) {
  frames_.remove(FrameKey(key.uid, key.source));
  const QString lease_key = FrameKey(key.uid, key.source);

  const auto clear_item = [this, &key](const application::VideoSceneItem& item) {
    if (item.uid != key.uid) return;
    VideoTile* tile = tiles_.value(TileKey(item), nullptr);
    if (tile == nullptr) return;
    if (item.source == key.source) {
      tile->AttachMainSurface(nullptr);
    }
    if (key.source == domain::VideoSource::Camera &&
        item.source == domain::VideoSource::Screen &&
        item.has_camera_pip) {
      tile->AttachPipSurface(nullptr);
    }
  };
  for (const auto& item : scene_.primary) clear_item(item);
  for (const auto& item : scene_.secondary) clear_item(item);
  surface_leases_.erase(lease_key);
}

bool VideoGrid::IsSurfaceRenderable(
    const domain::VideoStreamKey& key) const {
  const auto state = health_monitor_.StateFor(key, NowMs());
  return state == application::FrameHealthState::Waiting ||
         state == application::FrameHealthState::Healthy;
}

domain::VideoStreamKey VideoGrid::StreamKey(
    const application::VideoSceneItem& item) {
  return domain::VideoStreamKey{item.uid, item.source};
}

VideoTile* VideoGrid::EnsureTile(
    const application::VideoSceneItem& item) {
  const QString id = TileKey(item);
  VideoTile* tile = tiles_.value(id, nullptr);
  if (tile != nullptr) return tile;
  tile = new VideoTile(this);
  tiles_.insert(id, tile);
  connect(tile, &VideoTile::Selected, this, &VideoGrid::MemberSelected);
  connect(tile, &VideoTile::FocusRequested, this,
          &VideoGrid::MemberFocusRequested);
  return tile;
}

QString VideoGrid::TileKey(const application::VideoSceneItem& item) {
  return QString::fromStdString(application::VideoSceneKey(item));
}

void VideoGrid::ApplyPlacement() {
  while (QLayoutItem* item = layout_engine_->takeAt(0)) delete item;
  for (int row = 0; row < placed_rows_; ++row) {
    layout_engine_->setRowStretch(row, 0);
  }
  for (int column = 0; column < placed_columns_; ++column) {
    layout_engine_->setColumnStretch(column, 0);
  }
  for (auto it = tiles_.begin(); it != tiles_.end(); ++it) {
    it.value()->hide();
  }
  if (layout_plan_.cells.empty()) {
    placed_rows_ = 0;
    placed_columns_ = 0;
    return;
  }

  for (const auto& cell : layout_plan_.cells) {
    VideoTile* tile = tiles_.value(QString::fromStdString(cell.key));
    if (tile == nullptr) continue;
    layout_engine_->addWidget(tile, cell.row, cell.column, cell.row_span,
                              cell.column_span);
    tile->show();
  }
  for (int row = 0; row < layout_plan_.rows; ++row) {
    layout_engine_->setRowStretch(row, 1);
  }
  for (int column = 0; column < layout_plan_.columns; ++column) {
    layout_engine_->setColumnStretch(column, 1);
  }
  placed_rows_ = layout_plan_.rows;
  placed_columns_ = layout_plan_.columns;
}

void VideoGrid::resizeEvent(QResizeEvent* event) {
  QWidget::resizeEvent(event);
  ApplyPlacement();
}

void VideoGrid::paintEvent(QPaintEvent* event) {
  Q_UNUSED(event);
  if (!layout_plan_.cells.empty()) return;
  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing);
  QPainterPath path;
  path.addRoundedRect(QRectF(rect()).adjusted(10, 10, -10, -10), 14, 14);
  painter.setPen(QPen(QColor(QStringLiteral("#2d3e4c")), 1, Qt::DashLine));
  painter.setBrush(QColor(13, 19, 25, 150));
  painter.drawPath(path);
  painter.setPen(QColor(QStringLiteral("#8798a6")));
  painter.drawText(rect(), Qt::AlignCenter,
                   QStringLiteral("暂无视频成员"));
}

}  // namespace rtcwin::ui
