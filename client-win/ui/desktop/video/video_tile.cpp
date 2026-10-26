#include "video/video_tile.h"

#include "theme/theme.h"

#include <QEnterEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QResizeEvent>

namespace talk::desktop {

VideoTile::VideoTile(VideoSurfaceFactory surface_factory, QWidget* parent)
    : QWidget(parent), surface_(surface_factory(this)) {
  setAttribute(Qt::WA_StyledBackground, false);
  setMouseTracking(true);
  setCursor(Qt::PointingHandCursor);
  setMinimumSize(150, 100);

  surface_->setAttribute(Qt::WA_TransparentForMouseEvents, true);
  surface_->hide();
  if (auto* image = qobject_cast<ImageVideoSurface*>(surface_)) {
    image->SetMirrored(true);
  }

  const auto make_label = [this](const QString& object_name) {
    auto* label = new QLabel(this);
    label->setObjectName(object_name);
    label->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    return label;
  };
  name_ = make_label(QStringLiteral("VideoTileName"));
  name_->setStyleSheet(
      QStringLiteral("background:rgba(6,11,16,220);color:#eef5f8;"
                     "border:1px solid rgba(87,112,132,160);"
                     "border-radius:6px;padding:3px 8px;font-weight:600;"));
  badge_ = make_label(QStringLiteral("VideoTileBadge"));
  badge_->setStyleSheet(
      QStringLiteral("background:rgba(57,42,20,230);color:#ffd27d;"
                     "border:1px solid #76501d;border-radius:5px;"
                     "padding:2px 6px;font-size:10px;font-weight:600;"));
  mic_ = make_label(QStringLiteral("VideoTileStatus"));
  camera_ = make_label(QStringLiteral("VideoTileStatus"));
  name_->hide();
  badge_->hide();
  mic_->hide();
  camera_->hide();
}

void VideoTile::SetModel(const VideoTileModel& model) {
  model_ = model;
  if (auto* image = qobject_cast<ImageVideoSurface*>(surface_)) {
    image->SetMirrored(model_.self);
  }
  UpdateOverlays();
  PositionOverlays();
  update();
}

void VideoTile::SetFrame(const QImage& frame) {
  if (frame.isNull()) {
    ClearFrame();
    return;
  }
  surface_->SetFrame(frame);
  if (!has_frame_) {
    has_frame_ = true;
    surface_->show();
    surface_->lower();
  }
  update();
}

void VideoTile::ClearFrame() {
  if (!has_frame_) return;
  has_frame_ = false;
  surface_->ClearFrame();
  surface_->hide();
  update();
}

void VideoTile::SetSelected(bool selected) {
  if (selected_ == selected) return;
  selected_ = selected;
  update();
}

void VideoTile::SetCompact(bool compact) {
  if (compact_ == compact) return;
  compact_ = compact;
  setMinimumSize(compact ? QSize(96, 64) : QSize(150, 100));
  UpdateOverlays();
  PositionOverlays();
}

void VideoTile::SetPresentation(bool presentation) {
  if (presentation_ == presentation) return;
  presentation_ = presentation;
  setCursor(presentation ? Qt::ArrowCursor : Qt::PointingHandCursor);
  UpdateOverlays();
  PositionOverlays();
  update();
}

void VideoTile::UpdateOverlays() {
  if (name_ == nullptr) return;
  name_->setText(Title());
  name_->adjustSize();
  name_->show();

  QString badge;
  if (presentation_) {
    badge = QStringLiteral("共享画面");
  } else if (!model_.self && model_.role == QStringLiteral("host")) {
    badge = QStringLiteral("主持人");
  } else if (model_.screenOn && model_.self) {
    badge = QStringLiteral("我的共享");
  }
  badge_->setText(badge);
  badge_->setVisible(!badge.isEmpty());
  badge_->adjustSize();

  mic_->setPixmap(Theme::pixmap(
      model_.micMuted ? Theme::Icon::MicOff : Theme::Icon::Mic,
      compact_ ? 13 : 15,
      model_.micMuted ? QColor(QStringLiteral("#ff8585"))
                      : QColor(QStringLiteral("#d7e4ec"))));
  camera_->setPixmap(Theme::pixmap(
      model_.cameraOn ? Theme::Icon::Camera : Theme::Icon::CameraOff,
      compact_ ? 13 : 15,
      model_.cameraOn ? QColor(QStringLiteral("#d7e4ec"))
                      : QColor(QStringLiteral("#ff8585"))));
  mic_->show();
  camera_->show();
}

void VideoTile::PositionOverlays() {
  if (name_ == nullptr || surface_ == nullptr) return;
  surface_->setGeometry(rect().adjusted(1, 1, -1, -1));
  surface_->lower();

  const int margin = compact_ ? 8 : 11;
  name_->move(margin, height() - name_->height() - margin);
  int top = margin;
  if (badge_->isVisible()) {
    badge_->move(margin, top);
    top += badge_->height() + 4;
  }

  int right = width() - margin;
  if (camera_->isVisible()) {
    right -= camera_->width();
    camera_->move(right, height() - camera_->height() - margin);
    right -= 6;
  }
  if (mic_->isVisible()) {
    right -= mic_->width();
    mic_->move(right, height() - mic_->height() - margin);
  }
  name_->raise();
  badge_->raise();
  mic_->raise();
  camera_->raise();
}

QString VideoTile::Title() const {
  const QString name = model_.displayName.isEmpty() ? model_.uid
                                                     : model_.displayName;
  return model_.self ? QStringLiteral("%1（我）").arg(name) : name;
}

void VideoTile::paintEvent(QPaintEvent* event) {
  Q_UNUSED(event);
  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing);
  QPainterPath path;
  path.addRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 12, 12);
  painter.setClipPath(path);
  painter.fillPath(path, QColor(QStringLiteral("#0c1218")));

  if (!has_frame_) {
    QLinearGradient gradient(rect().topLeft(), rect().bottomRight());
    gradient.setColorAt(0, QColor(QStringLiteral("#182630")));
    gradient.setColorAt(1, QColor(QStringLiteral("#0b1117")));
    painter.fillPath(path, gradient);
    const int diameter = qBound(54, qMin(width(), height()) / 3, 94);
    const QRect avatar_rect((width() - diameter) / 2,
                            (height() - diameter) / 2 - 6, diameter,
                            diameter);
    painter.setPen(QPen(QColor(190, 221, 240, 34), 1));
    painter.setBrush(QColor(QStringLiteral("#223746")));
    painter.drawEllipse(avatar_rect);
    QFont font = painter.font();
    font.setPixelSize(qMax(20, diameter / 3));
    font.setWeight(QFont::Bold);
    painter.setFont(font);
    painter.setPen(QColor(QStringLiteral("#c6dded")));
    painter.drawText(avatar_rect, Qt::AlignCenter,
                     Theme::avatarText(model_.displayName.isEmpty()
                                           ? model_.uid
                                           : model_.displayName));
    QFont hint = painter.font();
    hint.setPixelSize(11);
    hint.setWeight(QFont::Normal);
    painter.setFont(hint);
    painter.setPen(QColor(QStringLiteral("#7c8d9b")));
    painter.drawText(QRect(0, avatar_rect.bottom() + 8, width(), 18),
                     Qt::AlignCenter,
                     model_.cameraOn ? QStringLiteral("等待视频帧")
                                     : QStringLiteral("摄像头关闭"));
  }

  painter.setClipping(false);
  QColor border(QStringLiteral("#22313e"));
  if (hovered_) border = QColor(QStringLiteral("#3c586c"));
  if (model_.speaking) border = QColor(QStringLiteral("#46d4a0"));
  if (selected_) border = QColor(QStringLiteral("#55c2ff"));
  painter.setPen(QPen(border, selected_ ? 2.0 : 1.0));
  painter.setBrush(Qt::NoBrush);
  painter.drawPath(path);
}

void VideoTile::resizeEvent(QResizeEvent* event) {
  QWidget::resizeEvent(event);
  PositionOverlays();
}

void VideoTile::mousePressEvent(QMouseEvent* event) {
  if (!presentation_ && event->button() == Qt::LeftButton) {
    emit selected(model_.uid);
  }
  QWidget::mousePressEvent(event);
}

void VideoTile::mouseDoubleClickEvent(QMouseEvent* event) {
  if (!presentation_ && event->button() == Qt::LeftButton) {
    emit selected(model_.uid);
    emit focusRequested(model_.uid);
  }
  QWidget::mouseDoubleClickEvent(event);
}

void VideoTile::enterEvent(QEvent* event) {
  QWidget::enterEvent(event);
  hovered_ = true;
  update();
}

void VideoTile::leaveEvent(QEvent* event) {
  QWidget::leaveEvent(event);
  hovered_ = false;
  update();
}

}  // namespace talk::desktop
