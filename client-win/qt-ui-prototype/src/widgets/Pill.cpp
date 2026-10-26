#include "widgets/Pill.h"

#include <QFontMetrics>
#include <QMouseEvent>
#include <QPainter>

Pill::Pill(const QString &text, const QString &tone, QWidget *parent)
    : QWidget(parent), m_text(text), m_tone(tone)
{
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    setAttribute(Qt::WA_Hover);
}

void Pill::setText(const QString &text) { m_text = text; updateGeometry(); update(); }
void Pill::setTone(const QString &tone) { m_tone = tone; update(); }
void Pill::setIcon(Theme::Icon icon, bool visible) { m_icon = icon; m_hasIcon = visible; updateGeometry(); update(); }
void Pill::setClickable(bool clickable)
{
    m_clickable = clickable;
    setCursor(clickable ? Qt::PointingHandCursor : Qt::ArrowCursor);
}

QSize Pill::sizeHint() const
{
    const QFontMetrics metrics(font());
    // Icon occupies 14 px plus a 5 px gap; keep extra room for glyph overhang
    // and font fallback so values such as "6 人" and "3/9" are never clipped.
    return QSize(metrics.horizontalAdvance(m_text) + (m_hasIcon ? 43 : 24), 28);
}
void Pill::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    QColor background(QStringLiteral("#11171d"));
    QColor border(QStringLiteral("#2d3d4d"));
    QColor text(QStringLiteral("#8a9aa8"));
    if (m_tone == QStringLiteral("accent")) {
        background = QColor(85, 194, 255, 28); border = QColor(85, 194, 255, 88); text = QColor(QStringLiteral("#70d5ff"));
    } else if (m_tone == QStringLiteral("ok")) {
        background = QColor(53, 208, 127, 28); border = QColor(53, 208, 127, 84); text = QColor(QStringLiteral("#66e4a3"));
    } else if (m_tone == QStringLiteral("warn")) {
        background = QColor(255, 180, 84, 24); border = QColor(255, 180, 84, 88); text = QColor(QStringLiteral("#ffc477"));
    } else if (m_tone == QStringLiteral("danger")) {
        background = QColor(255, 107, 107, 24); border = QColor(255, 107, 107, 86); text = QColor(QStringLiteral("#ff9292"));
    } else if (m_tone == QStringLiteral("step")) {
        background = QColor(QStringLiteral("#0f151b")); border = QColor(QStringLiteral("#263442")); text = QColor(QStringLiteral("#8a9aa8"));
    } else if (m_tone == QStringLiteral("stepActive")) {
        background = QColor(85, 194, 255, 26); border = QColor(85, 194, 255, 100); text = QColor(QStringLiteral("#70d5ff"));
    } else if (m_tone == QStringLiteral("strong")) {
        text = QColor(QStringLiteral("#e8edf2"));
        background = QColor(QStringLiteral("#121920"));
    }
    if (m_clickable && m_hovered) {
        background = background.lighter(118);
        border = border.lighter(125);
    }
    painter.setPen(QPen(border, 1));
    painter.setBrush(background);
    painter.drawRoundedRect(rect().adjusted(0, 1, -1, -2), 13, 13);

    QRectF content = rect().adjusted(10, 0, -10, 0);
    if (m_hasIcon) {
        Theme::drawIcon(painter, m_icon, QRectF(content.left(), 7, 14, 14), text, 1.6);
        content.setLeft(content.left() + 19);
    }
    painter.setFont(font());
    painter.setPen(text);
    painter.drawText(content, Qt::AlignVCenter | Qt::AlignLeft, m_text);
}

void Pill::enterEvent(QEvent *event)
{
    m_hovered = true;
    update();
    QWidget::enterEvent(event);
}

void Pill::leaveEvent(QEvent *event)
{
    m_hovered = false;
    update();
    QWidget::leaveEvent(event);
}
void Pill::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_clickable && event->button() == Qt::LeftButton && rect().contains(event->pos())) {
        emit clicked();
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}