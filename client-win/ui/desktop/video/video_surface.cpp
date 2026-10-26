#include "video/video_surface.h"

#include <QPainter>
#include <QPaintEvent>

namespace talk::desktop {

VideoSurface::VideoSurface(QWidget* parent) : QWidget(parent) {
  setAttribute(Qt::WA_OpaquePaintEvent, true);
  setMinimumSize(80, 60);
}

ImageVideoSurface::ImageVideoSurface(QWidget* parent) : VideoSurface(parent) {
  SetPlaceholder(QStringLiteral("等待视频帧"));
}

void ImageVideoSurface::SetFrame(const QImage& frame) {
  if (frame.isNull()) {
    ClearFrame();
    return;
  }
  frame_ = frame;
  update();
  emit frameChanged();
}

void ImageVideoSurface::ClearFrame() {
  if (frame_.isNull()) return;
  frame_ = QImage();
  update();
  emit frameChanged();
}

void ImageVideoSurface::SetMirrored(bool mirrored) {
  if (mirrored_ == mirrored) return;
  mirrored_ = mirrored;
  update();
}

void ImageVideoSurface::SetPlaceholder(const QString& text) {
  placeholder_ = text;
  update();
}

void ImageVideoSurface::paintEvent(QPaintEvent* event) {
  Q_UNUSED(event);
  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing);
  painter.setRenderHint(QPainter::SmoothPixmapTransform);
  painter.fillRect(rect(), QColor(QStringLiteral("#070b0f")));

  if (frame_.isNull()) {
    painter.setPen(QColor(QStringLiteral("#647584")));
    painter.drawText(rect(), Qt::AlignCenter, placeholder_);
    return;
  }

  const QSize target_size =
      frame_.size().scaled(size(), Qt::KeepAspectRatioByExpanding);
  const QRect target(QPoint((width() - target_size.width()) / 2,
                            (height() - target_size.height()) / 2),
                     target_size);
  if (mirrored_) {
    painter.save();
    painter.translate(width(), 0);
    painter.scale(-1.0, 1.0);
    painter.drawImage(target, frame_);
    painter.restore();
  } else {
    painter.drawImage(target, frame_);
  }
}

VideoSurface* CreateImageVideoSurface(QWidget* parent) {
  return new ImageVideoSurface(parent);
}

}  // namespace talk::desktop
