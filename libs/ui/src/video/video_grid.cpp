#include "rtcwin/ui/video/video_grid.h"

#include <QGridLayout>
#include <QPainter>
#include <QPainterPath>
#include <QResizeEvent>
#include <QSet>

#include <utility>

namespace rtcwin::ui {
namespace {

QString FrameKey(const std::string& uid, domain::VideoSource source) {
  return QString::fromStdString(uid) + QLatin1Char('|') +
         QString::fromStdString(domain::VideoSourceName(source));
}

}  // namespace

VideoGrid::VideoGrid(QWidget* parent) : QWidget(parent) {
  setMinimumSize(360, 240);
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
  layout_engine_ = new QGridLayout(this);
  layout_engine_->setContentsMargins(0, 0, 0, 0);
  layout_engine_->setSpacing(10);
}

void VideoGrid::SetState(const domain::SessionSnapshot& snapshot) {
  snapshot_ = snapshot;
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
  auto stored = std::make_shared<domain::VideoFrame>(std::move(copy));
  // Build the key after the move-completed object exists. Keeping key
  // construction separate from the move avoids unspecified argument
  // evaluation order and prevents a frame from being cached under an empty
  // UID or a stale source.
  frames_.insert(FrameKey(stored->uid, stored->source), stored);
  ApplyFrameToMatchingTiles(*stored);
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
  const auto apply_item =
      [this, &active_keys](const application::VideoSceneItem& item,
                           bool compact) {
    VideoTile* tile = EnsureTile(item);
    active_keys.insert(TileKey(item));
    tile->SetMember(MemberForUid(item.uid), item.self, item.selected,
                    compact);
    tile->SetSource(item.source, item.has_camera_pip);
    ApplyFrame(tile, item);
  };

  const bool compact_primary =
      scene_.mode != application::VideoSceneMode::Grid;
  for (const auto& item : scene_.primary) {
    apply_item(item, compact_primary);
  }
  for (const auto& item : scene_.secondary) {
    apply_item(item, true);
  }

  for (auto it = tiles_.begin(); it != tiles_.end();) {
    if (!active_keys.contains(it.key())) {
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
}

void VideoGrid::ApplyFrame(
    VideoTile* tile, const application::VideoSceneItem& item) {
  if (tile == nullptr) return;
  if (item.source == domain::VideoSource::Screen) {
    if (auto frame = frames_.value(
            FrameKey(item.uid, domain::VideoSource::Screen), nullptr)) {
      tile->SetFrame(*frame);
    } else {
      tile->ClearFrame();
    }
    if (item.has_camera_pip) {
      if (auto camera = frames_.value(
              FrameKey(item.uid, domain::VideoSource::Camera), nullptr)) {
        tile->SetPipFrame(*camera);
      } else {
        tile->ClearPipFrame();
      }
    } else {
      tile->ClearPipFrame();
    }
    return;
  }

  tile->ClearPipFrame();
  if (auto frame = frames_.value(
          FrameKey(item.uid, domain::VideoSource::Camera), nullptr)) {
    tile->SetFrame(*frame);
  } else {
    tile->ClearFrame();
  }
}

void VideoGrid::ApplyFrameToMatchingTiles(
    const domain::VideoFrame& frame) {
  const auto apply_item =
      [this, &frame](const application::VideoSceneItem& item) {
    const bool primary = item.source == frame.source;
    const bool camera_pip =
        frame.source == domain::VideoSource::Camera &&
        item.source == domain::VideoSource::Screen &&
        item.has_camera_pip;
    if (!primary && !camera_pip) return;
    VideoTile* tile = tiles_.value(TileKey(item), nullptr);
    if (tile == nullptr) return;
    if (primary) tile->SetFrame(frame);
    if (camera_pip) tile->SetPipFrame(frame);
  };
  for (const auto& item : scene_.primary) apply_item(item);
  for (const auto& item : scene_.secondary) apply_item(item);
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
