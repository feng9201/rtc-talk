#include "rtcwin/ui/widgets/icon_button.h"

#include <QPainter>

namespace rtcwin::ui {

IconButton::IconButton(theme::Icon icon, QWidget* parent)
    : QAbstractButton(parent), icon_(icon) {
  setCursor(Qt::PointingHandCursor);
  setFocusPolicy(Qt::StrongFocus);
  setFixedSize(diameter_, diameter_);
}

void IconButton::SetIcon(theme::Icon icon) {
  icon_ = icon;
  update();
}

void IconButton::SetTone(const QString& tone) {
  tone_ = tone;
  update();
}

void IconButton::SetDiameter(int diameter) {
  diameter_ = diameter;
  setFixedSize(diameter_, diameter_);
  update();
}

QSize IconButton::sizeHint() const { return QSize(diameter_, diameter_); }

void IconButton::enterEvent(QEvent* event) {
  hovered_ = true;
  update();
  QAbstractButton::enterEvent(event);
}

void IconButton::leaveEvent(QEvent* event) {
  hovered_ = false;
  update();
  QAbstractButton::leaveEvent(event);
}

void IconButton::paintEvent(QPaintEvent* event) {
  Q_UNUSED(event);
  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing);

  QColor background(QStringLiteral("#151e27"));
  QColor border(QStringLiteral("#2d3d4d"));
  QColor foreground(QStringLiteral("#e8edf2"));
  if (tone_ == QStringLiteral("danger")) {
    background = QColor(QStringLiteral("#321b1d"));
    border = QColor(QStringLiteral("#553036"));
    foreground = QColor(QStringLiteral("#ff8686"));
  } else if (tone_ == QStringLiteral("active")) {
    background = QColor(QStringLiteral("#173747"));
    border = QColor(QStringLiteral("#2a6684"));
    foreground = QColor(QStringLiteral("#70d5ff"));
  } else if (tone_ == QStringLiteral("muted")) {
    background = QColor(QStringLiteral("#291b1c"));
    border = QColor(QStringLiteral("#4b2b2d"));
    foreground = QColor(QStringLiteral("#ff9a9a"));
  } else if (tone_ == QStringLiteral("ghost")) {
    background = Qt::transparent;
    border = Qt::transparent;
  }
  if (hovered_ && isEnabled()) {
    background = background.lighter(118);
    border = border.lighter(125);
  }
  if (isDown()) background = background.darker(112);
  if (!isEnabled()) {
    background = QColor(QStringLiteral("#11161b"));
    border = QColor(QStringLiteral("#202932"));
    foreground = QColor(QStringLiteral("#53616d"));
  }

  painter.setPen(QPen(border, 1));
  painter.setBrush(background);
  painter.drawEllipse(rect().adjusted(1, 1, -1, -1));
  const int icon_size = qRound(diameter_ * .43);
  const QRect icon_rect((width() - icon_size) / 2,
                        (height() - icon_size) / 2, icon_size, icon_size);
  painter.drawPixmap(icon_rect, theme::Pixmap(icon_, icon_size, foreground));
}

}  // namespace rtcwin::ui