#include "rtcwin/muxplayer_adapter/mux_video_surface.h"

#include <frame_pump.h>
#include <gl_play_widget.h>

#include <libavutil/frame.h>

#include <QPaintEvent>
#include <QPainter>
#include <QPainterPath>
#include <QRegion>
#include <QResizeEvent>

#include <algorithm>
#include <cstring>

namespace rtcwin::muxplayer_adapter {

MuxVideoSurface::MuxVideoSurface(QWidget* parent) : QWidget(parent) {
  setAttribute(Qt::WA_OpaquePaintEvent, true);
  setMinimumSize(120, 80);
}

MuxVideoSurface::~MuxVideoSurface() { Close(); }

bool MuxVideoSurface::Open() {
  if (opened_) return true;
  pump_ = std::make_unique<muxplay::FramePump>();
  gl_widget_ = new GLPlayWidget(this);
  gl_widget_->setAttribute(Qt::WA_TransparentForMouseEvents, true);
  gl_widget_->setDecodeMode(muxplay::DecodeMode::Software);
  gl_widget_->setKeepLastFrame(false);
  gl_widget_->setSoftwareFramePump(pump_.get());
  gl_widget_->hide();
  pump_->setOnFrameReady([this] {
    if (gl_widget_ != nullptr) gl_widget_->requestNewFrame();
  });
  LayoutSurface();
  opened_ = true;
  return true;
}

bool MuxVideoSurface::SubmitFrame(const domain::VideoFrame& frame) {
  if (!opened_ && !Open()) return false;
  if (frame.width <= 0 || frame.height <= 0 || frame.stride_y <= 0 ||
      frame.stride_u <= 0 || frame.stride_v <= 0 ||
      frame.y.size() < static_cast<size_t>(frame.stride_y) * frame.height ||
      frame.u.size() <
          static_cast<size_t>(frame.stride_u) * ((frame.height + 1) / 2) ||
      frame.v.size() <
          static_cast<size_t>(frame.stride_v) * ((frame.height + 1) / 2)) {
    return false;
  }

  frame_width_ = frame.width;
  frame_height_ = frame.height;
  AVFrame* av_frame = av_frame_alloc();
  if (av_frame == nullptr) return false;
  av_frame->format = AV_PIX_FMT_YUV420P;
  av_frame->width = frame.width;
  av_frame->height = frame.height;
  av_frame->pts = frame.timestamp_us / 1000;
  if (av_frame_get_buffer(av_frame, 32) < 0) {
    av_frame_free(&av_frame);
    return false;
  }

  const auto copy_plane = [](uint8_t* dst, int dst_stride,
                             const std::vector<uint8_t>& src,
                             int src_stride, int width, int height) {
    for (int row = 0; row < height; ++row) {
      std::memcpy(dst + static_cast<size_t>(row) * dst_stride,
                  src.data() + static_cast<size_t>(row) * src_stride, width);
    }
  };
  copy_plane(av_frame->data[0], av_frame->linesize[0], frame.y,
             frame.stride_y, frame.width, frame.height);
  copy_plane(av_frame->data[1], av_frame->linesize[1], frame.u,
             frame.stride_u, (frame.width + 1) / 2, (frame.height + 1) / 2);
  copy_plane(av_frame->data[2], av_frame->linesize[2], frame.v,
             frame.stride_v, (frame.width + 1) / 2, (frame.height + 1) / 2);

  pump_->consume(av_frame);
  av_frame_free(&av_frame);
  has_frame_ = true;
  LayoutSurface();
  if (gl_widget_ != nullptr) gl_widget_->show();
  return true;
}

void MuxVideoSurface::SetPlaceholder(const QString& text) {
  placeholder_ = text;
  update();
}

void MuxVideoSurface::SetCornerRadius(int radius) {
  corner_radius_ = std::max(0, radius);
  ApplyMask();
  LayoutSurface();
  update();
}

void MuxVideoSurface::Clear() {
  has_frame_ = false;
  frame_width_ = 0;
  frame_height_ = 0;
  if (pump_) pump_->Clear();
  if (gl_widget_ != nullptr) {
    gl_widget_->hide();
    gl_widget_->clearFrame();
  }
  update();
}

void MuxVideoSurface::Close() {
  has_frame_ = false;
  frame_width_ = 0;
  frame_height_ = 0;
  if (gl_widget_ != nullptr) {
    gl_widget_->hide();
    delete gl_widget_;
    gl_widget_ = nullptr;
  }
  pump_.reset();
  opened_ = false;
}

void MuxVideoSurface::paintEvent(QPaintEvent* event) {
  Q_UNUSED(event);
  QPainter painter(this);
  painter.fillRect(rect(), QColor(QStringLiteral("#070b0f")));
  if (has_frame_) return;
  painter.setPen(QColor(QStringLiteral("#637483")));
  painter.drawText(rect(), Qt::AlignCenter, placeholder_);
}

void MuxVideoSurface::resizeEvent(QResizeEvent* event) {
  QWidget::resizeEvent(event);
  ApplyMask();
  LayoutSurface();
}

void MuxVideoSurface::LayoutSurface() {
  if (gl_widget_ == nullptr) return;
  QRect target = rect();
  if (frame_width_ > 0 && frame_height_ > 0 && !target.isEmpty()) {
    const QSize scaled = QSize(frame_width_, frame_height_)
                             .scaled(target.size(),
                                     Qt::KeepAspectRatioByExpanding);
    target = QRect(QPoint((width() - scaled.width()) / 2,
                          (height() - scaled.height()) / 2),
                   scaled);
  }
  gl_widget_->setGeometry(target);
}

void MuxVideoSurface::ApplyMask() {
  if (corner_radius_ <= 0 || width() <= 0 || height() <= 0) {
    clearMask();
    return;
  }
  QPainterPath path;
  path.addRoundedRect(QRectF(rect()), corner_radius_, corner_radius_);
  setMask(QRegion(path.toFillPolygon().toPolygon()));
}

}  // namespace rtcwin::muxplayer_adapter
