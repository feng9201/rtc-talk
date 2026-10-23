#include "video/MockVideoSurface.h"

#include "base/Theme.h"

#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>

#include <cmath>

namespace {
QString initialFor(const QString &name)
{
    const QString trimmed = name.trimmed();
    return trimmed.isEmpty() ? QStringLiteral("?") : trimmed.left(1).toUpper();
}

QColor mix(const QColor &a, const QColor &b, qreal t)
{
    return QColor::fromRgbF(a.redF() * (1.0 - t) + b.redF() * t,
                            a.greenF() * (1.0 - t) + b.greenF() * t,
                            a.blueF() * (1.0 - t) + b.blueF() * t,
                            a.alphaF() * (1.0 - t) + b.alphaF() * t);
}
} // namespace

MockVideoSurface::MockVideoSurface(QWidget *parent)
    : VideoSurface(parent)
{
    setAttribute(Qt::WA_OpaquePaintEvent, false);
}

void MockVideoSurface::setMember(const QString &name, bool cameraEnabled, bool sharing, bool self, const QString &source)
{
    m_name = name;
    m_cameraEnabled = cameraEnabled;
    m_sharing = sharing;
    m_self = self;
    m_source = source;
    update();
}

void MockVideoSurface::setPreview(bool preview)
{
    m_preview = preview;
    update();
}

void MockVideoSurface::setCompact(bool compact)
{
    m_compact = compact;
    update();
}

QRgb MockVideoSurface::accentColor() const
{
    uint hash = 2166136261u;
    for (const QChar ch : m_name) hash = (hash ^ ch.unicode()) * 16777619u;
    const int hue = 188 + static_cast<int>(hash % 46u);
    return QColor::fromHsv(hue, 150, 210).rgb();
}

void MockVideoSurface::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::TextAntialiasing, true);

    QPainterPath clip;
    clip.addRoundedRect(QRectF(rect()).adjusted(1, 1, -1, -1), 12, 12);
    painter.setClipPath(clip);

    const QColor accent(accentColor());
    QLinearGradient background(0, 0, width(), height());
    if (m_sharing) {
        background.setColorAt(0, QColor(QStringLiteral("#102432")));
        background.setColorAt(1, QColor(QStringLiteral("#071019")));
    } else {
        background.setColorAt(0, mix(QColor(QStringLiteral("#162731")), accent, 0.28));
        background.setColorAt(0.58, QColor(QStringLiteral("#101b23")));
        background.setColorAt(1, QColor(QStringLiteral("#071016")));
    }
    painter.fillRect(rect(), background);

    QRadialGradient glow(QPointF(width() * 0.7, height() * 0.25), qMax(width(), height()) * 0.75);
    glow.setColorAt(0, QColor(accent.red(), accent.green(), accent.blue(), m_cameraEnabled ? 42 : 28));
    glow.setColorAt(1, QColor(0, 0, 0, 0));
    painter.fillRect(rect(), glow);

    painter.setPen(QPen(QColor(255, 255, 255, 13), 1));
    const int grid = m_compact ? 30 : 48;
    for (int x = grid; x < width(); x += grid) painter.drawLine(x, 0, x, height());
    for (int y = grid; y < height(); y += grid) painter.drawLine(0, y, width(), y);

    if (m_sharing) {
        paintSharePlaceholder(painter);
    } else if (!m_cameraEnabled) {
        paintAvatarPlaceholder(painter);
    } else {
        paintCameraPlaceholder(painter);
    }
}

void MockVideoSurface::paintSharePlaceholder(QPainter &painter)
{
    const qreal margin = qMax<qreal>(22, width() * 0.07);
    QRectF window(margin, margin, width() - margin * 2, height() - margin * 2);
    if (window.height() > window.width() * 0.68)
        window.setHeight(qMax<qreal>(120, window.width() * 0.68));

    painter.setPen(QPen(QColor(QStringLiteral("#335267")), 1));
    painter.setBrush(QColor(QStringLiteral("#101b24")));
    painter.drawRoundedRect(window, 9, 9);

    QRectF titleBar = window.adjusted(0, 0, 0, -window.height() + 34);
    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(QStringLiteral("#152430")));
    painter.drawRoundedRect(titleBar, 9, 9);
    painter.drawRect(titleBar.adjusted(0, 20, 0, 0));
    const QColor dots[] = {QColor(QStringLiteral("#4b687c")), QColor(QStringLiteral("#405e70")), QColor(QStringLiteral("#385466"))};
    for (int i = 0; i < 3; ++i) {
        painter.setBrush(dots[i]);
        painter.drawEllipse(QPointF(titleBar.left() + 18 + i * 16, titleBar.center().y()), 4, 4);
    }

    QRectF leftPanel(window.left() + 26, titleBar.bottom() + 28, window.width() * 0.48, window.height() * 0.56);
    QRectF rightPanel(window.left() + window.width() * 0.55, titleBar.bottom() + 28,
                      window.width() * 0.38, window.height() * 0.56);
    painter.setPen(QPen(QColor(QStringLiteral("#29465a")), 1));
    painter.setBrush(QColor(QStringLiteral("#0b141c")));
    painter.drawRoundedRect(leftPanel, 8, 8);
    painter.drawRoundedRect(rightPanel, 8, 8);

    painter.setPen(Qt::NoPen);
    painter.setBrush(QColor(QStringLiteral("#55c2ff")));
    const qreal x = leftPanel.left() + 18;
    painter.drawRoundedRect(QRectF(x, leftPanel.top() + 22, leftPanel.width() * 0.42, 9), 4, 4);
    painter.setBrush(QColor(QStringLiteral("#36576d")));
    for (int i = 0; i < 4; ++i) {
        const qreal y = leftPanel.top() + 52 + i * 25;
        painter.drawRoundedRect(QRectF(x, y, leftPanel.width() * (0.86 - i * 0.09), 7), 3, 3);
    }
    painter.setBrush(QColor(QStringLiteral("#36576d")));
    for (int i = 0; i < 4; ++i) {
        painter.drawRoundedRect(QRectF(rightPanel.left() + 16, rightPanel.top() + 20 + i * 24,
                                      rightPanel.width() * (0.72 - i * 0.05), 7), 3, 3);
    }

    if (!m_compact) {
        painter.setPen(QColor(QStringLiteral("#7d97a8")));
        QFont font = painter.font();
        font.setPixelSize(12);
        painter.setFont(font);
        painter.drawText(window.adjusted(12, 0, -18, -12), Qt::AlignRight | Qt::AlignBottom,
                         m_source == QStringLiteral("system") ? QStringLiteral("系统音频共享") : QStringLiteral("屏幕共享"));
    }
}

void MockVideoSurface::paintAvatarPlaceholder(QPainter &painter)
{
    if (m_preview) {
        painter.setPen(QPen(QColor(255, 255, 255, 24), 1));
        painter.setBrush(QColor(QStringLiteral("#17212a")));
        const qreal radius = qMin(width(), height()) * 0.22;
        painter.drawEllipse(QPointF(width() / 2.0, height() / 2.0), radius, radius);
        painter.setPen(QColor(QStringLiteral("#e8edf2")));
        QFont font = painter.font();
        font.setPixelSize(qMax(28, static_cast<int>(radius * 0.78)));
        font.setWeight(QFont::DemiBold);
        painter.setFont(font);
        painter.drawText(rect(), Qt::AlignCenter, initialFor(m_name));
        return;
    }

    const qreal radius = qMin(width(), height()) * (m_compact ? 0.20 : 0.14);
    QRadialGradient halo(QPointF(width() / 2.0, height() / 2.0), radius * 2.5);
    halo.setColorAt(0, QColor(accentColor()).lighter(120));
    halo.setColorAt(1, QColor(0, 0, 0, 0));
    painter.setPen(Qt::NoPen);
    painter.setBrush(halo);
    painter.drawEllipse(QPointF(width() / 2.0, height() / 2.0), radius * 2.5, radius * 2.5);

    painter.setPen(QPen(QColor(QStringLiteral("#526a7c")), 1));
    painter.setBrush(QColor(QStringLiteral("#20303c")));
    painter.drawEllipse(QPointF(width() / 2.0, height() / 2.0), radius, radius);
    painter.setPen(QColor(QStringLiteral("#dbe5ec")));
    QFont font = painter.font();
    font.setPixelSize(qMax(18, static_cast<int>(radius * 0.82)));
    font.setWeight(QFont::DemiBold);
    painter.setFont(font);
    painter.drawText(rect(), Qt::AlignCenter, initialFor(m_name));
}

void MockVideoSurface::paintCameraPlaceholder(QPainter &painter)
{
    if (m_compact) return;

    const QSize iconSize(32, 32);
    const QRect iconRect(QPoint(width() / 2 - iconSize.width() / 2, height() / 2 - 28), iconSize);
    Theme::drawIcon(painter, Theme::Icon::Camera, iconRect, QColor(255, 255, 255, 36), 1.5);

    painter.setPen(QColor(255, 255, 255, 38));
    QFont font = painter.font();
    font.setPixelSize(12);
    painter.setFont(font);
    painter.drawText(QRect(0, height() / 2 + 10, width(), 22), Qt::AlignCenter,
                     QStringLiteral("视频画面 · mock"));
}