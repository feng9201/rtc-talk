#include "video/VideoSurface.h"

#include <QPainter>
#include <QPaintEvent>
#include <QPainterPath>

VideoSurface::VideoSurface(QWidget *parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_OpaquePaintEvent, false);
    setMinimumSize(80, 60);
}

VideoSurface::~VideoSurface() = default;

void VideoSurface::setFrame(const QImage &frame)
{
    m_frame = frame;
    update();
}

void VideoSurface::clearFrame()
{
    m_frame = QImage();
    update();
}

void VideoSurface::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

    QPainterPath clip;
    clip.addRoundedRect(QRectF(rect()).adjusted(1, 1, -1, -1), 12, 12);
    painter.setClipPath(clip);
    painter.fillRect(rect(), QColor(QStringLiteral("#0a0f14")));

    if (!m_frame.isNull()) {
        const QSize scaled = m_frame.size().scaled(size(), Qt::KeepAspectRatioByExpanding);
        const QRect target(QPoint((width() - scaled.width()) / 2, (height() - scaled.height()) / 2), scaled);
        painter.drawImage(target, m_frame);
    } else {
        painter.setPen(QColor(QStringLiteral("#34414d")));
        painter.drawText(rect(), Qt::AlignCenter, QStringLiteral("等待视频帧"));
    }
}