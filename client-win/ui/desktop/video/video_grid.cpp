#include "video/video_grid.h"

#include "theme/theme.h"

#include <QDateTime>
#include <QPainter>
#include <QPainterPath>
#include <QResizeEvent>

#include <algorithm>
#include <cmath>

namespace talk::desktop {
namespace {

const VideoStreamView* BestStream(const QVector<VideoStreamView>& streams,
                                  const QString& uid, VideoSource source) {
  const VideoStreamView* best = nullptr;
  for (const VideoStreamView& stream : streams) {
    if (stream.uid != uid || stream.source != source) continue;
    if (best == nullptr || stream.lastFrameMs > best->lastFrameMs) {
      best = &stream;
    }
  }
  return best != nullptr && !best->paused ? best : nullptr;
}

const VideoStreamView* NewestStream(const QVector<VideoStreamView>& streams,
                                    const QString& uid, VideoSource source) {
  const VideoStreamView* best = nullptr;
  for (const VideoStreamView& stream : streams) {
    if (stream.uid != uid || stream.source != source) continue;
    if (best == nullptr || stream.lastFrameMs > best->lastFrameMs) {
      best = &stream;
    }
  }
  return best;
}

bool IsVideoMember(const VideoTileModel& model) {
  return model.cameraOn || model.screenOn || model.receiving ||
         model.screenReceiving;
}

}  // namespace

VideoGrid::VideoGrid(VideoSurfaceFactory surface_factory, QWidget* parent)
    : QWidget(parent), surface_factory_(std::move(surface_factory)) {
  setMinimumSize(320, 220);
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void VideoGrid::SetState(const SessionSnapshot& state,
                         const QVector<VideoStreamView>& streams) {
  state_ = state;
  streams_ = streams;
  if (!state_.joined) frames_.clear();
  SyncTiles();
  Recalculate();
}

void VideoGrid::SetStreams(const QVector<VideoStreamView>& streams) {
  streams_ = streams;
  SyncTiles();
  Recalculate();
}

void VideoGrid::SetLocalFrame(const QImage& frame) {
  FrameReady(QStringLiteral("local:camera"), frame);
}

void VideoGrid::SetLayoutMode(VideoLayoutMode mode) {
  if (layout_ == mode) return;
  layout_ = mode;
  Recalculate();
}

void VideoGrid::SetFilterMode(VideoFilterMode mode) {
  if (filter_ == mode) return;
  filter_ = mode;
  Recalculate();
}

void VideoGrid::FrameReady(const QString& mid, const QImage& frame) {
  if (mid.isEmpty() || frame.isNull()) return;
  frames_.insert(mid, frame);
  for (VideoTile* tile : tiles_) {
    if (tile->cameraMid() == mid) tile->SetFrame(frame);
  }
  if (share_tile_ != nullptr && share_mid_ == mid) {
    share_tile_->SetFrame(frame);
  }
}

void VideoGrid::FrameCleared(const QString& mid) {
  frames_.remove(mid);
  for (VideoTile* tile : tiles_) {
    if (tile->cameraMid() == mid) tile->ClearFrame();
  }
  if (share_tile_ != nullptr && share_mid_ == mid) {
    share_tile_->ClearFrame();
  }
}

void VideoGrid::SyncTiles() {
  const QVector<MemberTile> members = BuildMembers();
  QHash<QString, bool> used;
  for (const MemberTile& member : members) {
    VideoTile* tile = TileFor(member.model.uid);
    if (tile == nullptr) {
      tile = new VideoTile(surface_factory_, this);
      tiles_.insert(member.model.uid, tile);
      connect(tile, &VideoTile::selected, this,
              &VideoGrid::memberSelected);
      connect(tile, &VideoTile::focusRequested, this,
              [this](const QString& uid) {
                emit memberSelected(uid);
                emit memberFocusRequested(uid);
              });
    }
    tile->SetModel(member.model);
    tile->SetSelected(member.model.uid == state_.selectedUid);
    ApplyFrame(tile, member.model.cameraMid);
    used.insert(member.model.uid, true);
  }

  for (auto it = tiles_.begin(); it != tiles_.end();) {
    if (!used.contains(it.key())) {
      it.value()->deleteLater();
      it = tiles_.erase(it);
    } else {
      ++it;
    }
  }

  tile_order_.clear();
  for (const MemberTile& member : members) {
    tile_order_.append(member.model.uid);
  }

  const MemberTile share = BuildShare(members);
  if (!share.model.uid.isEmpty()) {
    if (share_tile_ == nullptr) {
      share_tile_ = new VideoTile(surface_factory_, this);
      share_tile_->SetPresentation(true);
    }
    share_tile_->SetModel(share.model);
    share_uid_ = share.model.uid;
    share_mid_ = share.model.screenMid;
    ApplyFrame(share_tile_, share_mid_);
    share_tile_->show();
  } else {
    share_uid_.clear();
    share_mid_.clear();
    if (share_tile_ != nullptr) {
      share_tile_->hide();
      share_tile_->deleteLater();
      share_tile_ = nullptr;
    }
  }
}

QVector<VideoGrid::MemberTile> VideoGrid::BuildMembers() const {
  QVector<PeerSnapshot> people;
  PeerSnapshot self;
  self.uid = state_.selfUid;
  self.displayName = state_.selfDisplayName;
  self.role = state_.selfRole;
  self.audioMuted = state_.micMuted;
  self.speaking = state_.floor.selfHasFloor;
  self.cameraOn = state_.cameraOn;
  self.screenOn = state_.sharing;
  people.append(self);
  people += state_.peers;

  QVector<MemberTile> result;
  for (const PeerSnapshot& peer : people) {
    if (peer.uid.isEmpty()) continue;
    const bool is_self = peer.uid == state_.selfUid;
    const VideoStreamView* camera_meta =
        NewestStream(streams_, peer.uid, VideoSource::Camera);
    const VideoStreamView* screen_meta =
        NewestStream(streams_, peer.uid, VideoSource::Screen);
    const VideoStreamView* camera =
        BestStream(streams_, peer.uid, VideoSource::Camera);
    const VideoStreamView* screen =
        BestStream(streams_, peer.uid, VideoSource::Screen);
    MemberTile item;
    item.model.uid = peer.uid;
    item.model.displayName = peer.displayName;
    item.model.role = peer.role;
    item.model.self = is_self;
    item.model.micMuted = peer.audioMuted;
    item.model.speaking = peer.speaking;
    item.model.cameraOn =
        is_self ? state_.cameraOn
                : peer.cameraOn &&
                      (camera_meta == nullptr || !camera_meta->paused);
    item.model.screenOn =
        is_self ? state_.sharing
                : peer.screenOn && screen_meta != nullptr &&
                      !screen_meta->paused;
    item.model.cameraMid =
        is_self ? QStringLiteral("local:camera")
                : (camera != nullptr ? camera->mid : QString());
    item.model.screenMid = screen != nullptr ? screen->mid : QString();
    item.model.receiving =
        is_self ? state_.cameraOn && frames_.contains(item.model.cameraMid)
                : camera != nullptr && camera->receiving;
    item.model.screenReceiving =
        is_self ? state_.sharing
                : screen != nullptr && screen->receiving;
    if (is_self && state_.sharing && item.model.screenMid.isEmpty()) {
      item.model.screenMid = QStringLiteral("local:screen");
    }
    item.screen_available = item.model.screenOn;
    result.append(item);
  }
  return result;
}

QVector<VideoGrid::MemberTile> VideoGrid::VisibleMembers(
    const QVector<MemberTile>& members) const {
  if (filter_ == VideoFilterMode::All) return members;
  QVector<MemberTile> result;
  for (const MemberTile& member : members) {
    if (IsVideoMember(member.model)) result.append(member);
  }
  return result;
}

VideoGrid::MemberTile VideoGrid::BuildShare(
    const QVector<MemberTile>& members) const {
  const MemberTile* selected = nullptr;
  for (const MemberTile& member : members) {
    if (!member.screen_available) continue;
    if (member.model.uid == state_.selectedUid) {
      selected = &member;
      break;
    }
    if (selected == nullptr) selected = &member;
  }
  if (selected == nullptr) return MemberTile{};
  MemberTile share = *selected;
  share.model.cameraOn = true;
  share.model.receiving = share.model.screenReceiving;
  return share;
}

void VideoGrid::ApplyFrame(VideoTile* tile, const QString& mid) {
  if (tile == nullptr) return;
  if (!mid.isEmpty() && frames_.contains(mid)) {
    tile->SetFrame(frames_.value(mid));
  } else {
    tile->ClearFrame();
  }
}

void VideoGrid::PlaceTile(VideoTile* tile, const QRect& rect, bool compact,
                          bool show) {
  if (tile == nullptr) return;
  tile->SetCompact(compact);
  tile->setGeometry(rect);
  tile->setVisible(show);
}

VideoTile* VideoGrid::TileFor(const QString& uid) const {
  return tiles_.value(uid, nullptr);
}

void VideoGrid::Recalculate() {
  if (width() <= 0 || height() <= 0) return;
  for (VideoTile* tile : tiles_) tile->hide();
  if (share_tile_ != nullptr) share_tile_->hide();

  const QVector<MemberTile> all_members = BuildMembers();
  const QVector<MemberTile> members = VisibleMembers(all_members);
  if (members.isEmpty() && share_tile_ == nullptr) {
    update();
    return;
  }

  const QRect content = rect().adjusted(1, 1, -1, -1);
  const int gap = 10;

  if (share_tile_ != nullptr && !share_uid_.isEmpty()) {
    const int strip_height =
        std::clamp(qRound(content.height() * 0.15), 82, 112);
    const QRect main_rect(content.left(), content.top(), content.width(),
                          content.height() - strip_height - gap);
    const QRect strip_rect(content.left(), main_rect.bottom() + gap + 1,
                           content.width(), strip_height);
    PlaceTile(share_tile_, main_rect, false, true);
    share_tile_->raise();

    QVector<MemberTile> strip = all_members;
    if (filter_ == VideoFilterMode::VideoOnly) {
      strip = VisibleMembers(all_members);
    }
    if (!strip.isEmpty()) {
      const int item_gap = 8;
      const int item_width = std::clamp(
          (strip_rect.width() - item_gap * (strip.size() - 1)) / strip.size(),
          92, 176);
      for (int i = 0; i < strip.size(); ++i) {
        VideoTile* tile = TileFor(strip.at(i).model.uid);
        PlaceTile(tile,
                  QRect(strip_rect.left() + i * (item_width + item_gap),
                        strip_rect.top(), item_width, strip_rect.height()),
                  true, true);
        if (tile != nullptr) tile->raise();
      }
    }
    update();
    return;
  }

  if (members.isEmpty()) {
    update();
    return;
  }

  if (all_members.size() == 2 && members.size() == 2 &&
      layout_ == VideoLayoutMode::Grid) {
    VideoTile* remote = nullptr;
    VideoTile* self = nullptr;
    for (const MemberTile& member : members) {
      if (member.model.self) {
        self = TileFor(member.model.uid);
      } else {
        remote = TileFor(member.model.uid);
      }
    }
    if (remote != nullptr) {
      PlaceTile(remote, content, false, true);
      remote->raise();
    }
    if (self != nullptr) {
      const int pip_width =
          std::clamp(qRound(content.width() * 0.24), 180, 280);
      const int pip_height = qRound(pip_width * 9.0 / 16.0);
      const int margin = 14;
      PlaceTile(self,
                QRect(content.right() - pip_width - margin,
                      content.bottom() - pip_height - margin, pip_width,
                      pip_height),
                true, true);
      self->raise();
    }
    update();
    return;
  }

  if (layout_ == VideoLayoutMode::Focus && members.size() > 1) {
    const MemberTile* focus = nullptr;
    for (const MemberTile& member : members) {
      if (member.model.uid == state_.selectedUid) {
        focus = &member;
        break;
      }
    }
    if (focus == nullptr) focus = &members.first();
    const int strip_height =
        std::clamp(qRound(content.height() * 0.15), 82, 112);
    const QRect main_rect(content.left(), content.top(), content.width(),
                          content.height() - strip_height - gap);
    const QRect strip_rect(content.left(), main_rect.bottom() + gap + 1,
                           content.width(), strip_height);
    PlaceTile(TileFor(focus->model.uid), main_rect, false, true);
    if (VideoTile* tile = TileFor(focus->model.uid)) tile->raise();

    QVector<MemberTile> strip;
    for (const MemberTile& member : members) {
      if (member.model.uid != focus->model.uid) strip.append(member);
    }
    if (!strip.isEmpty()) {
      const int item_gap = 8;
      const int item_width = std::clamp(
          (strip_rect.width() - item_gap * (strip.size() - 1)) / strip.size(),
          88, 168);
      for (int i = 0; i < strip.size(); ++i) {
        VideoTile* tile = TileFor(strip.at(i).model.uid);
        PlaceTile(tile,
                  QRect(strip_rect.left() + i * (item_width + item_gap),
                        strip_rect.top(), item_width, strip_rect.height()),
                  true, true);
        if (tile != nullptr) tile->raise();
      }
    }
    update();
    return;
  }

  int columns = 1;
  if (members.size() > 1 && members.size() <= 4) columns = 2;
  else if (members.size() > 4) columns = 3;
  const int rows =
      static_cast<int>(std::ceil(members.size() / static_cast<qreal>(columns)));
  const int tile_width = (content.width() - gap * (columns - 1)) / columns;
  const int tile_height = (content.height() - gap * (rows - 1)) / rows;
  const bool compact = members.size() > 4;
  for (int i = 0; i < members.size(); ++i) {
    VideoTile* tile = TileFor(members.at(i).model.uid);
    const int column = i % columns;
    const int row = i / columns;
    PlaceTile(tile,
              QRect(content.left() + column * (tile_width + gap),
                    content.top() + row * (tile_height + gap), tile_width,
                    tile_height),
              compact, true);
    if (tile != nullptr) tile->raise();
  }
  update();
}

void VideoGrid::resizeEvent(QResizeEvent* event) {
  QWidget::resizeEvent(event);
  Recalculate();
}

void VideoGrid::paintEvent(QPaintEvent* event) {
  Q_UNUSED(event);
  if (share_tile_ != nullptr || !VisibleMembers(BuildMembers()).isEmpty()) {
    return;
  }
  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing);
  const QRectF box = QRectF(rect()).adjusted(10, 10, -10, -10);
  QPainterPath path;
  path.addRoundedRect(box, 14, 14);
  painter.setPen(QPen(QColor(QStringLiteral("#2d3d4d")), 1, Qt::DashLine));
  painter.setBrush(QColor(15, 20, 26, 115));
  painter.drawPath(path);
  Theme::drawIcon(painter, Theme::Icon::CameraOff,
                  QRectF(width() / 2.0 - 22, height() / 2.0 - 72, 44, 44),
                  QColor(QStringLiteral("#5c6b79")), 1.6);
  QFont title_font = painter.font();
  title_font.setPixelSize(15);
  title_font.setWeight(QFont::DemiBold);
  painter.setFont(title_font);
  painter.setPen(QColor(QStringLiteral("#e8edf2")));
  painter.drawText(QRect(0, height() / 2 - 18, width(), 28),
                   Qt::AlignCenter,
                   state_.joined ? QStringLiteral("暂无视频成员")
                                 : QStringLiteral("等待加入房间"));
  QFont hint_font = painter.font();
  hint_font.setPixelSize(12);
  hint_font.setWeight(QFont::Normal);
  painter.setFont(hint_font);
  painter.setPen(QColor(QStringLiteral("#8a9aa8")));
  painter.drawText(
      QRect(0, height() / 2 + 14, width(), 42),
      Qt::AlignHCenter | Qt::TextWordWrap,
      state_.joined ? QStringLiteral("切换为“全部成员”可查看头像占位")
                    : QStringLiteral("进入房间后将在这里显示真实视频流"));
}

}  // namespace talk::desktop
