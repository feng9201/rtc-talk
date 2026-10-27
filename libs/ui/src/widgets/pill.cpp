#include "rtcwin/ui/widgets/pill.h"

#include <QStyle>
#include <QVariant>

namespace rtcwin::ui {

Pill::Pill(const QString& text, const QString& tone, QWidget* parent)
    : QLabel(text, parent) {
  setObjectName(QStringLiteral("Pill"));
  setAlignment(Qt::AlignCenter);
  SetTone(tone);
}

void Pill::SetTone(const QString& tone) {
  setProperty("tone", tone);
  style()->unpolish(this);
  style()->polish(this);
}

void Pill::SetIcon(const QPixmap& icon) {
  setPixmap(icon);
  setText(text());
}

}  // namespace rtcwin::ui
