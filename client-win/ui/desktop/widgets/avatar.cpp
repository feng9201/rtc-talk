#include "widgets/avatar.h"

#include <QLinearGradient>
#include <QPainter>

namespace talk::desktop {

Avatar::Avatar(QWidget* parent) : QWidget(parent) {
  setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
}

Avatar::Avatar(const QString& text, int diameter, QWidget* parent)
    : Avatar(parent) {
  text_ = text.trimmed().isEmpty() ? QStringLiteral("?")
                                   : text.trimmed().left(1).toUpper();
  SetDiameter(diameter);
}

void Avatar::SetText(const QString& text) {
  const QString trimmed = text.trimmed();
  text_ = trimmed.isEmpty() ? QStringLiteral("?") : trimmed.left(1).toUpper();
  update();
}

void Avatar::SetDiameter(int diameter) {
  diameter_ = qMax(20, diameter);
  setFixedSize(diameter_, diameter_);
  update();
}

QSize Avatar::sizeHint() const {
  return QSize(diameter_, diameter_);
}

void Avatar::paintEvent(QPaintEvent* event) {
  Q_UNUSED(event);
  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing);
  QLinearGradient gradient(rect().topLeft(), rect().bottomRight());
  gradient.setColorAt(0, QColor(QStringLiteral("#2b4355")));
  gradient.setColorAt(1, QColor(QStringLiteral("#192731")));
  painter.setPen(QPen(QColor(190, 220, 240, 28), 1));
  painter.setBrush(gradient);
  painter.drawEllipse(rect().adjusted(1, 1, -1, -1));
  QFont font = painter.font();
  font.setPixelSize(qMax(11, qRound(diameter_ * 0.4)));
  font.setWeight(QFont::Bold);
  painter.setFont(font);
  painter.setPen(QColor(QStringLiteral("#c5ddec")));
  painter.drawText(rect(), Qt::AlignCenter, text_);
}

}  // namespace talk::desktop
