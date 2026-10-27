#include "rtcwin/ui/widgets/control_button.h"

#include <QStyle>
#include <QVariant>

namespace rtcwin::ui {

ControlButton::ControlButton(theme::Icon icon, const QString& text,
                             QWidget* parent)
    : QToolButton(parent), icon_(icon) {
  setObjectName(QStringLiteral("ControlButton"));
  setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
  setIconSize(QSize(22, 22));
  setMinimumSize(66, 62);
  setCursor(Qt::PointingHandCursor);
  SetText(text);
  SetTone(QStringLiteral("default"));
}

void ControlButton::SetIcon(theme::Icon icon) {
  icon_ = icon;
  const QColor color =
      property("tone").toString() == QStringLiteral("active")
          ? QColor(QStringLiteral("#55c2ff"))
          : QColor(QStringLiteral("#d6e2ea"));
  setIcon(QIcon(theme::Pixmap(icon_, 22, color)));
}

void ControlButton::SetText(const QString& text) {
  setText(text);
}

void ControlButton::SetTone(const QString& tone) {
  setProperty("tone", tone);
  style()->unpolish(this);
  style()->polish(this);
  SetIcon(icon_);
}

}  // namespace rtcwin::ui
