#include "rtcwin/ui/video/video_grid.h"

#include <QGridLayout>
#include <QPainter>
#include <QPainterPath>
#include <QResizeEvent>
#include <QSet>

#include <cmath>

namespace rtcwin::ui {

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

void VideoGrid::SetLayoutMode(domain::StageLayout layout) {
  layout_ = layout;
  ApplyPlacement();
}

void VideoGrid::SetFilter(domain::StageFilter filter) {
  filter_ = filter;
  ApplyPlacement();
  update();
}

QVector<domain::PeerState> VideoGrid::VisibleMembers() const {
  QVector<domain::PeerState> result;
  domain::PeerState self;
  self.uid = snapshot_.self_uid;
  self.display_name = snapshot_.self_display_name;
  self.role = snapshot_.self_role;
  self.audio_muted = snapshot_.mic_muted;
  self.speaking = snapshot_.floor.self_has_floor;
  self.camera_on = snapshot_.camera_on;
  self.screen_on = snapshot_.sharing;
  self.audio_level = snapshot_.self_audio_level;
  if (!self.uid.empty() || !self.display_name.empty()) result.push_back(self);
  for (const auto& peer : snapshot_.peers) result.push_back(peer);
  if (filter_ == domain::StageFilter::VideoOnly) {
    QVector<domain::PeerState> video;
    for (const auto& member : result) {
      if (member.camera_on || member.screen_on) video.push_back(member);
    }
    return video;
  }
  return result;
}

void VideoGrid::Rebuild() {
  const auto members = VisibleMembers();
  QSet<QString> ids;
  for (const auto& member : members) {
    auto* tile = EnsureTile(member);
    ids.insert(QString::fromStdString(member.uid));
    tile->SetMember(member,
                    member.uid == snapshot_.self_uid,
                    QString::fromStdString(member.uid) ==
                        QString::fromStdString(snapshot_.selected_uid));
  }
  for (auto it = tiles_.begin(); it != tiles_.end();) {
    if (!ids.contains(it.key())) {
      it.value()->deleteLater();
      it = tiles_.erase(it);
    } else {
      ++it;
    }
  }
}

VideoTile* VideoGrid::EnsureTile(const domain::PeerState& member) {
  const QString id = QString::fromStdString(member.uid);
  VideoTile* tile = tiles_.value(id, nullptr);
  if (tile != nullptr) return tile;
  tile = new VideoTile(this);
  tiles_.insert(id, tile);
  connect(tile, &VideoTile::Selected, this, &VideoGrid::MemberSelected);
  connect(tile, &VideoTile::FocusRequested, this,
          &VideoGrid::MemberFocusRequested);
  return tile;
}

void VideoGrid::ApplyPlacement() {
  while (QLayoutItem* item = layout_engine_->takeAt(0)) delete item;
  const auto members = VisibleMembers();
  for (const auto& member : members) {
    if (auto* tile = tiles_.value(QString::fromStdString(member.uid))) {
      tile->hide();
    }
  }
  if (members.empty()) return;

  if (layout_ == domain::StageLayout::Focus && members.size() > 1) {
    const QString focus_uid =
        snapshot_.selected_uid.empty()
            ? QString::fromStdString(members.first().uid)
            : QString::fromStdString(snapshot_.selected_uid);
    VideoTile* focus = tiles_.value(focus_uid);
    if (focus == nullptr) focus = tiles_.value(
        QString::fromStdString(members.first().uid));
    layout_engine_->addWidget(focus, 0, 0, 3, 2);
    focus->show();
    int row = 0;
    for (const auto& member : members) {
      if (QString::fromStdString(member.uid) == focus_uid) continue;
      if (auto* tile = tiles_.value(QString::fromStdString(member.uid))) {
        layout_engine_->addWidget(tile, row++, 2);
        tile->show();
      }
    }
    return;
  }

  int columns = 1;
  if (members.size() > 1 && members.size() <= 4) columns = 2;
  else if (members.size() > 4) columns = 3;
  for (int i = 0; i < members.size(); ++i) {
    auto* tile = tiles_.value(QString::fromStdString(members.at(i).uid));
    layout_engine_->addWidget(tile, i / columns, i % columns);
    tile->show();
  }
}

void VideoGrid::resizeEvent(QResizeEvent* event) {
  QWidget::resizeEvent(event);
  ApplyPlacement();
}

void VideoGrid::paintEvent(QPaintEvent* event) {
  Q_UNUSED(event);
  if (!VisibleMembers().empty()) return;
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
