#include "widgets/Avatar.h"

#include "base/Theme.h"

#include <QPainter>
#include <QLinearGradient>

Avatar::Avatar(QWidget *parent)
    : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
}

Avatar::Avatar(const QString &text, int diameter, QWidget *parent)
    : Avatar(parent)
{
    m_text = text;
    setDiameter(diameter);
}

void Avatar::setText(const QString &text)
{
    m_text = Theme::avatarText(text);
    update();
}

void Avatar::setDiameter(int diameter)
{
    m_diameter = diameter;
    setFixedSize(diameter, diameter);
    update();
}

QSize Avatar::sizeHint() const
{
    return QSize(m_diameter, m_diameter);
}

void Avatar::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    QLinearGradient gradient(rect().topLeft(), rect().bottomRight());
    gradient.setColorAt(0, QColor(QStringLiteral("#2b4355")));
    gradient.setColorAt(1, QColor(QStringLiteral("#192731")));
    painter.setPen(QPen(QColor(190, 220, 240, 28), 1));
    painter.setBrush(gradient);
    painter.drawEllipse(rect().adjusted(1, 1, -1, -1));
    QFont font = painter.font();
    font.setPixelSize(qMax(11, qRound(m_diameter * .4)));
    font.setWeight(QFont::Bold);
    painter.setFont(font);
    painter.setPen(QColor(QStringLiteral("#c5ddec")));
    painter.drawText(rect(), Qt::AlignCenter, m_text);
}