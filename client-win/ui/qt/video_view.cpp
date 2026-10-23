// File: ui/qt/video_view.cpp
// Brief: 实现远端视频的合并刷新与等比绘制。
#include "video_view.h"

#include <QColor>
#include <QPaintEvent>
#include <QPainter>
#include <QTimer>
#include <utility>

namespace {

// 与 30fps 对齐：媒体线程 15fps 时也只重绘一次最新帧。
constexpr int kRefreshIntervalMs = 33;

}  // namespace

VideoView::VideoView(QWidget* parent) : QWidget(parent) {
  setMinimumSize(480, 270);
  setAttribute(Qt::WA_OpaquePaintEvent);
  timer_ = new QTimer(this);
  timer_->setInterval(kRefreshIntervalMs);
  connect(timer_, &QTimer::timeout, this, [this] { ApplyPending(); });
  timer_->start();
}

void VideoView::PushFrame(const QImage& frame) {
  if (frame.isNull()) {
    return;
  }
  pending_ = frame;
}

void VideoView::Clear() {
  pending_ = QImage();
  frame_ = QImage();
  update();
}

void VideoView::SetLabel(const QString& label) {
  if (label_ == label) {
    return;
  }
  label_ = label;
  update();
}

void VideoView::ApplyPending() {
  if (pending_.isNull()) {
    return;
  }
  frame_ = std::move(pending_);
  pending_ = QImage();
  update();
}

void VideoView::paintEvent(QPaintEvent* event) {
  Q_UNUSED(event);
  QPainter painter(this);
  painter.fillRect(rect(), Qt::black);
  if (frame_.isNull()) {
    painter.setPen(QColor(0x9e, 0x9e, 0x9e));
    painter.drawText(rect(), Qt::AlignCenter,
                     QStringLiteral("等待远端视频"));
    return;
  }
  // 等比缩放居中：保持对端桌面比例，多余区域留黑边。
  const QSize target = frame_.size().scaled(size(), Qt::KeepAspectRatio);
  const QRect area(QPoint((width() - target.width()) / 2,
                          (height() - target.height()) / 2),
                   target);
  painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
  painter.drawImage(area, frame_);
  if (!label_.isEmpty()) {
    // 多路宫格下靠角标区分对端；半透明底避免亮画面看不清。
    const QRect badge(8, 8, fontMetrics().horizontalAdvance(label_) + 12, 22);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(0, 0, 0, 140));
    painter.drawRoundedRect(badge, 4, 4);
    painter.setPen(Qt::white);
    painter.drawText(badge, Qt::AlignCenter, label_);
  }
}
