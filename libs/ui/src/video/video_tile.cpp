#include "rtcwin/ui/video/video_tile.h"

#include "rtcwin/muxplayer_adapter/mux_video_surface.h"
#include "rtcwin/ui/theme/theme.h"

#include <QLabel>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QResizeEvent>

#include <algorithm>

namespace rtcwin::ui {

VideoTile::VideoTile(QWidget* parent) : QWidget(parent) {
  setMouseTracking(true);
  setCursor(Qt::PointingHandCursor);
  setMinimumSize(150, 100);

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

  pip_border_ = new QLabel(this);
  pip_border_->setObjectName(QStringLiteral("TileCameraPipBorder"));
  pip_border_->setAttribute(Qt::WA_TransparentForMouseEvents, true);
  pip_border_->setStyleSheet(
      QStringLiteral("background:transparent;border:1px solid #4f89b5;"
                     "border-radius:10px;"));
  pip_border_->hide();

  pip_label_ = new QLabel(QStringLiteral("摄像头"), this);
  pip_label_->setObjectName(QStringLiteral("TileCameraPipLabel"));
  pip_label_->setAttribute(Qt::WA_TransparentForMouseEvents, true);
  pip_label_->setStyleSheet(
      QStringLiteral("background:rgba(5,9,13,220);color:#e7eef4;"
                     "border-radius:5px;padding:2px 6px;font-size:10px;"));
  pip_label_->hide();
}

VideoTile::~VideoTile() {
  surface_ = nullptr;
  pip_surface_ = nullptr;
}

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

void VideoTile::SetSource(domain::VideoSource source, bool has_camera_pip) {
  source_ = source;
  has_camera_pip_ = has_camera_pip;
  if (!has_camera_pip_) {
    if (pip_surface_ != nullptr) pip_surface_->hide();
    if (pip_border_ != nullptr) pip_border_->hide();
    if (pip_label_ != nullptr) pip_label_->hide();
  } else if (pip_surface_ != nullptr) {
    pip_surface_->show();
    if (pip_border_ != nullptr) pip_border_->show();
    if (pip_label_ != nullptr) pip_label_->show();
  }
  UpdateOverlay();
  update();
}

void VideoTile::AttachMainSurface(
    muxplayer_adapter::MuxVideoSurface* surface) {
  if (surface_ == surface) return;
  surface_ = surface;
  if (surface_ != nullptr) {
    surface_->setParent(this);
    surface_->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    surface_->SetCornerRadius(11);
    surface_->setGeometry(rect().adjusted(1, 1, -1, -1));
    surface_->show();
    surface_->lower();
  }
  UpdateOverlay();
  update();
}

void VideoTile::AttachPipSurface(
    muxplayer_adapter::MuxVideoSurface* surface) {
  if (pip_surface_ == surface) return;
  pip_surface_ = surface;
  if (pip_surface_ != nullptr) {
    pip_surface_->setParent(this);
    pip_surface_->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    pip_surface_->SetCornerRadius(9);
    pip_surface_->SetPlaceholder(QStringLiteral("摄像头"));
    pip_surface_->show();
  }
  positionPipSurface();
  update();
}

void VideoTile::DetachSurfaces() {
  surface_ = nullptr;
  pip_surface_ = nullptr;
  if (pip_border_ != nullptr) pip_border_->hide();
  if (pip_label_ != nullptr) pip_label_->hide();
  update();
}

void VideoTile::SetFrame(const domain::VideoFrame& frame) {
  if (surface_ != nullptr) surface_->SubmitFrame(frame);
  update();
}

void VideoTile::SetPipFrame(const domain::VideoFrame& frame) {
  if (pip_surface_ != nullptr) {
    pip_surface_->SubmitFrame(frame);
    pip_surface_->show();
    if (pip_border_ != nullptr && has_camera_pip_) pip_border_->show();
    if (pip_label_ != nullptr && has_camera_pip_) pip_label_->show();
  }
  update();
}

void VideoTile::ClearFrame() {
  if (surface_ != nullptr) surface_->Clear();
  update();
}

void VideoTile::ClearPipFrame() {
  if (pip_surface_ != nullptr) {
    pip_surface_->Clear();
    pip_surface_->hide();
  }
  if (pip_border_ != nullptr) pip_border_->hide();
  if (pip_label_ != nullptr) pip_label_->hide();
  update();
}

void VideoTile::UpdateOverlay() {
  QString name = QString::fromStdString(member_.display_name);
  if (self_) name += QStringLiteral("（我）");
  name_->setText(name);
  name_->adjustSize();
  name_->show();

  QString status;
  if (source_ == domain::VideoSource::Screen) {
    status = QStringLiteral("共享中");
  } else if (member_.screen_on &&
             source_ == domain::VideoSource::Camera) {
    status = QStringLiteral("摄像头");
  } else if (!member_.audio_muted && member_.speaking) {
    status = QStringLiteral("发言中");
  } else if (!member_.camera_on) {
    status = QStringLiteral("摄像头关闭");
  }
  status_->setText(status);
  status_->setVisible(!status.isEmpty());
  status_->adjustSize();

  role_->setText(member_.role == domain::RoomRole::Host
                     ? QStringLiteral("主持人")
                     : QString());
  role_->setVisible(member_.role == domain::RoomRole::Host);
  role_->adjustSize();
  if (surface_ != nullptr) {
    surface_->SetPlaceholder(
        source_ == domain::VideoSource::Screen
            ? QStringLiteral("等待共享画面")
            : (compact_ ? QStringLiteral("视频")
                        : QStringLiteral("等待视频帧")));
  }
  positionOverlays();
}

void VideoTile::resizeEvent(QResizeEvent* event) {
  QWidget::resizeEvent(event);
  if (surface_ != nullptr) {
    surface_->setGeometry(rect().adjusted(1, 1, -1, -1));
  }
  positionPipSurface();
  positionOverlays();
}

void VideoTile::positionOverlays() {
  const int margin = compact_ ? 8 : 11;
  const int name_height = name_->height();
  const int status_height = status_->isVisible() ? status_->height() : 0;
  name_->move(margin, height() - name_height - margin);
  status_->move(margin, height() - name_height - status_height -
                           margin - (status_height > 0 ? 4 : 0));
  role_->move(margin, margin);
  if (pip_surface_ != nullptr) pip_surface_->raise();
  if (pip_border_ != nullptr) pip_border_->raise();
  if (pip_label_ != nullptr) pip_label_->raise();
  name_->raise();
  status_->raise();
  role_->raise();
}

void VideoTile::positionPipSurface() {
  if (pip_surface_ == nullptr) {
    if (pip_border_ != nullptr) pip_border_->hide();
    if (pip_label_ != nullptr) pip_label_->hide();
    return;
  }
  const int margin = compact_ ? 8 : 11;
  const int available_width = std::max(96, width() - margin * 2);
  const int available_height = std::max(64, height() - margin * 2);
  int pip_width =
      std::clamp(static_cast<int>(width() * 0.25), 128, 260);
  pip_width = std::min(pip_width, available_width);
  int pip_height = pip_width * 9 / 16;
  if (pip_height > available_height) {
    pip_height = available_height;
    pip_width = pip_height * 16 / 9;
  }
  const QRect pip_rect(width() - margin - pip_width,
                       height() - margin - pip_height, pip_width,
                       pip_height);
  pip_surface_->setGeometry(pip_rect);
  if (pip_border_ != nullptr) pip_border_->setGeometry(pip_rect);
  if (pip_label_ != nullptr) {
    pip_label_->setMaximumWidth(std::max(40, pip_width - 12));
    pip_label_->adjustSize();
    pip_label_->move(pip_rect.left() + 6, pip_rect.top() + 6);
  }
  if (has_camera_pip_) {
    pip_surface_->show();
    if (pip_border_ != nullptr) pip_border_->show();
    if (pip_label_ != nullptr) pip_label_->show();
  }
  pip_surface_->raise();
  if (pip_border_ != nullptr) pip_border_->raise();
  if (pip_label_ != nullptr) pip_label_->raise();
}

void VideoTile::paintEvent(QPaintEvent* event) {
  Q_UNUSED(event);
  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing);
  QPainterPath path;
  path.addRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 12, 12);
  painter.setClipPath(path);
  if (surface_ == nullptr || !surface_->has_frame()) {
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
