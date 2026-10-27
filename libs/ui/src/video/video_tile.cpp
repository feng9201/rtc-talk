#include "rtcwin/ui/video/video_tile.h"

#include "rtcwin/ui/theme/theme.h"

#include <QLabel>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QResizeEvent>

namespace rtcwin::ui {

VideoTile::VideoTile(QWidget* parent) : QWidget(parent) {
  setMouseTracking(true);
  setCursor(Qt::PointingHandCursor);
  setMinimumSize(150, 100);

  surface_ = new ImageVideoSurface(this);
  surface_->setAttribute(Qt::WA_TransparentForMouseEvents, true);

  const auto make_label = [this](const QString& object_name) {
    auto* label = new QLabel(this);
    label->setObjectName(object_name);
    label->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    label->setStyleSheet(
        QStringLiteral("background:rgba(5,9,13,220);color:#e7eef4;"
                       "border-radius:6px;padding:3px 7px;"));
    return label;
  };
  name_ = make_label(QStringLiteral("TileName"));
  status_ = make_label(QStringLiteral("TileStatus"));
  status_->setStyleSheet(
      QStringLiteral("background:rgba(9,14,20,205);color:#becbd5;"
                     "border-radius:5px;padding:2px 6px;font-size:10px;"));
  role_ = make_label(QStringLiteral("TileRole"));
  role_->setStyleSheet(
      QStringLiteral("background:rgba(56,43,20,225);color:#ffd37d;"
                     "border:1px solid #6f4e1f;border-radius:5px;"
                     "padding:2px 6px;font-size:10px;"));
  role_->hide();
}

VideoTile::~VideoTile() = default;

void VideoTile::SetMember(const domain::PeerState& member, bool self,
                          bool selected, bool compact) {
  member_ = member;
  uid_ = QString::fromStdString(member.uid);
  self_ = self;
  selected_ = selected;
  compact_ = compact;
  UpdateOverlay();
  update();
}

void VideoTile::SetSurface(VideoSurface* surface) {
  if (surface == nullptr || surface == surface_) return;
  if (surface_ != nullptr) surface_->deleteLater();
  surface_ = surface;
  surface_->setParent(this);
  surface_->setAttribute(Qt::WA_TransparentForMouseEvents, true);
  surface_->show();
  surface_->lower();
  surface_->setGeometry(rect());
}

void VideoTile::UpdateOverlay() {
  QString name = QString::fromStdString(member_.display_name);
  if (self_) name += QStringLiteral("（我）");
  name_->setText(name);
  name_->adjustSize();
  name_->show();

  QString status;
  if (member_.screen_on) status = QStringLiteral("共享中");
  else if (!member_.audio_muted && member_.speaking) status = QStringLiteral("发言中");
  else if (!member_.camera_on) status = QStringLiteral("摄像头关闭");
  status_->setText(status);
  status_->setVisible(!status.isEmpty());
  status_->adjustSize();

  role_->setText(member_.role == domain::RoomRole::Host
                     ? QStringLiteral("主持人")
                     : QString());
  role_->setVisible(member_.role == domain::RoomRole::Host);
  role_->adjustSize();
  if (auto* image = qobject_cast<ImageVideoSurface*>(surface_)) {
    image->SetPlaceholder(compact_ ? QStringLiteral("视频")
                                   : QStringLiteral("muxPlayer 画面接入点"));
  }
  positionOverlays();
}

void VideoTile::resizeEvent(QResizeEvent* event) {
  QWidget::resizeEvent(event);
  surface_->setGeometry(rect().adjusted(1, 1, -1, -1));
  positionOverlays();
}

void VideoTile::positionOverlays() {
  const int margin = compact_ ? 8 : 11;
  name_->move(margin, height() - name_->height() - margin);
  status_->move(margin, height() - status_->height() - name_->height() -
                           margin * 2);
  role_->move(margin, margin);
  name_->raise();
  status_->raise();
  role_->raise();
}

void VideoTile::paintEvent(QPaintEvent* event) {
  Q_UNUSED(event);
  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing);
  QPainterPath path;
  path.addRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 12, 12);
  painter.setClipPath(path);
  if (!surface_->HasFrame()) {
    QLinearGradient gradient(rect().topLeft(), rect().bottomRight());
    gradient.setColorAt(0, QColor(QStringLiteral("#18252f")));
    gradient.setColorAt(1, QColor(QStringLiteral("#0a1015")));
    painter.fillPath(path, gradient);
    const int diameter = qBound(48, qMin(width(), height()) / 3, 88);
    const QRect avatar((width() - diameter) / 2,
                       (height() - diameter) / 2 - 8, diameter, diameter);
    painter.setPen(QPen(QColor(190, 220, 238, 32), 1));
    painter.setBrush(QColor(QStringLiteral("#223746")));
    painter.drawEllipse(avatar);
    QFont font = painter.font();
    font.setPixelSize(qMax(18, diameter / 3));
    font.setBold(true);
    painter.setFont(font);
    painter.setPen(QColor(QStringLiteral("#c6dded")));
    painter.drawText(
        avatar, Qt::AlignCenter,
        theme::AvatarText(QString::fromStdString(member_.display_name)));
  }
  painter.setClipping(false);
  QColor border = selected_ ? QColor(QStringLiteral("#55c2ff"))
                            : QColor(QStringLiteral("#22313d"));
  if (member_.speaking) border = QColor(QStringLiteral("#53d3a0"));
  painter.setPen(QPen(border, selected_ ? 2.0 : 1.0));
  painter.setBrush(Qt::NoBrush);
  painter.drawPath(path);
}

void VideoTile::mousePressEvent(QMouseEvent* event) {
  if (event->button() == Qt::LeftButton) emit Selected(uid_);
  QWidget::mousePressEvent(event);
}

void VideoTile::mouseDoubleClickEvent(QMouseEvent* event) {
  if (event->button() == Qt::LeftButton) {
    emit Selected(uid_);
    emit FocusRequested(uid_);
  }
  QWidget::mouseDoubleClickEvent(event);
}

}  // namespace rtcwin::ui
