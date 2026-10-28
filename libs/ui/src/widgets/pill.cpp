#include "rtcwin/ui/widgets/pill.h"

#include <QFontMetrics>
#include <QPainter>

namespace rtcwin::ui {

Pill::Pill(const QString& text, const QString& tone, QWidget* parent)
    : QWidget(parent), text_(text), tone_(tone) {
  setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
}

void Pill::setText(const QString& text) {
  text_ = text;
  updateGeometry();
  update();
}

void Pill::SetTone(const QString& tone) {
  tone_ = tone;
  update();
}

QSize Pill::sizeHint() const {
  const QFontMetrics metrics(font());
  return QSize(metrics.horizontalAdvance(text_) + 24, 28);
}

void Pill::paintEvent(QPaintEvent* event) {
  Q_UNUSED(event);
  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing);

  QColor background(QStringLiteral("#11171d"));
  QColor border(QStringLiteral("#2d3d4d"));
  QColor text(QStringLiteral("#8a9aa8"));
  if (tone_ == QStringLiteral("accent")) {
    background = QColor(85, 194, 255, 28);
    border = QColor(85, 194, 255, 88);
    text = QColor(QStringLiteral("#70d5ff"));
  } else if (tone_ == QStringLiteral("ok")) {
    background = QColor(53, 208, 127, 28);
    border = QColor(53, 208, 127, 84);
    text = QColor(QStringLiteral("#66e4a3"));
  } else if (tone_ == QStringLiteral("warn")) {
    background = QColor(255, 180, 84, 24);
    border = QColor(255, 180, 84, 88);
    text = QColor(QStringLiteral("#ffc477"));
  } else if (tone_ == QStringLiteral("danger")) {
    background = QColor(255, 107, 107, 24);
    border = QColor(255, 107, 107, 86);
    text = QColor(QStringLiteral("#ff9292"));
  } else if (tone_ == QStringLiteral("step")) {
    background = QColor(QStringLiteral("#0f151b"));
    border = QColor(QStringLiteral("#263442"));
    text = QColor(QStringLiteral("#8a9aa8"));
  } else if (tone_ == QStringLiteral("stepActive")) {
    background = QColor(85, 194, 255, 26);
    border = QColor(85, 194, 255, 100);
    text = QColor(QStringLiteral("#70d5ff"));
  }

  painter.setPen(QPen(border, 1));
  painter.setBrush(background);
  painter.drawRoundedRect(rect().adjusted(0, 1, -1, -2), 13, 13);
  painter.setFont(font());
  painter.setPen(text);
  painter.drawText(rect().adjusted(10, 0, -10, 0),
                   Qt::AlignVCenter | Qt::AlignLeft, text_);
}

}  // namespace rtcwin::ui