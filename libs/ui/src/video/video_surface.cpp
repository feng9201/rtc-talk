#include "rtcwin/ui/video/video_surface.h"

#include <QPainter>
#include <QPaintEvent>

namespace rtcwin::ui {

VideoSurface::VideoSurface(QWidget* parent) : QWidget(parent) {
  setAttribute(Qt::WA_OpaquePaintEvent, true);
  setMinimumSize(120, 80);
}

ImageVideoSurface::ImageVideoSurface(QWidget* parent) : VideoSurface(parent) {}

void ImageVideoSurface::SetFrame(const QImage& frame) {
  frame_ = frame;
  update();
}

void ImageVideoSurface::Clear() {
  frame_ = QImage();
  update();
}

void ImageVideoSurface::SetPlaceholder(const QString& text) {
  placeholder_ = text;
  update();
}

void ImageVideoSurface::paintEvent(QPaintEvent* event) {
  Q_UNUSED(event);
  QPainter painter(this);
  painter.setRenderHint(QPainter::SmoothPixmapTransform);
  painter.fillRect(rect(), QColor(QStringLiteral("#070b0f")));
  if (frame_.isNull()) {
    painter.setPen(QColor(QStringLiteral("#637483")));
    painter.drawText(rect(), Qt::AlignCenter, placeholder_);
    return;
  }
  const QSize target =
      frame_.size().scaled(size(), Qt::KeepAspectRatioByExpanding);
  const QRect rect_target(QPoint((width() - target.width()) / 2,
                                 (height() - target.height()) / 2),
                         target);
  painter.drawImage(rect_target, frame_);
}

}  // namespace rtcwin::ui
