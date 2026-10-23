#include "widgets/IconButton.h"

#include <QPainter>

IconButton::IconButton(Theme::Icon icon, QWidget *parent)
    : QAbstractButton(parent), m_icon(icon)
{
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::StrongFocus);
    setFixedSize(m_diameter, m_diameter);
}

void IconButton::setIcon(Theme::Icon icon)
{
    m_icon = icon;
    update();
}

void IconButton::setTone(const QString &tone)
{
    m_tone = tone;
    update();
}

void IconButton::setDiameter(int diameter)
{
    m_diameter = diameter;
    setFixedSize(diameter, diameter);
    update();
}

QSize IconButton::sizeHint() const
{
    return QSize(m_diameter, m_diameter);
}

void IconButton::enterEvent(QEvent *event)
{
    m_hovered = true;
    update();
    QAbstractButton::enterEvent(event);
}

void IconButton::leaveEvent(QEvent *event)
{
    m_hovered = false;
    update();
    QAbstractButton::leaveEvent(event);
}

void IconButton::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    QColor background(QStringLiteral("#151e27"));
    QColor border(QStringLiteral("#2d3d4d"));
    QColor foreground(QStringLiteral("#e8edf2"));

    if (m_tone == QStringLiteral("danger")) {
        background = QColor(QStringLiteral("#321b1d"));
        border = QColor(QStringLiteral("#553036"));
        foreground = QColor(QStringLiteral("#ff8686"));
    } else if (m_tone == QStringLiteral("active")) {
        background = QColor(QStringLiteral("#173747"));
        border = QColor(QStringLiteral("#2a6684"));
        foreground = QColor(QStringLiteral("#70d5ff"));
    } else if (m_tone == QStringLiteral("muted")) {
        background = QColor(QStringLiteral("#291b1c"));
        border = QColor(QStringLiteral("#4b2b2d"));
        foreground = QColor(QStringLiteral("#ff9a9a"));
    } else if (m_tone == QStringLiteral("ghost")) {
        background = QColor(0, 0, 0, 0);
        border = QColor(0, 0, 0, 0);
    }

    if (m_hovered && isEnabled()) {
        background = background.lighter(118);
        border = border.lighter(125);
    }
    if (isDown()) background = background.darker(112);
    if (!isEnabled()) {
        background = QColor(QStringLiteral("#11161b"));
        border = QColor(QStringLiteral("#202932"));
        foreground = QColor(QStringLiteral("#53616d"));
    }

    const QRectF box = rect().adjusted(1, 1, -1, -1);
    painter.setPen(QPen(border, 1));
    painter.setBrush(background);
    painter.drawRoundedRect(box, m_diameter / 2.0, m_diameter / 2.0);

    const int iconSize = qRound(m_diameter * .43);
    const QRectF iconRect((width() - iconSize) / 2.0, (height() - iconSize) / 2.0, iconSize, iconSize);
    Theme::drawIcon(painter, m_icon, iconRect, foreground, 1.8);
}