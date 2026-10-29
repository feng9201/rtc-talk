#include "rtcwin/ui/widgets/control_button.h"

#include "rtcwin/ui/widgets/icon_button.h"

#include <QLabel>
#include <QVBoxLayout>

namespace rtcwin::ui {

ControlButton::ControlButton(theme::Icon icon, const QString& text,
                             QWidget* parent)
    : QWidget(parent), button_(new IconButton(icon, this)),
      label_(new QLabel(text, this)) {
  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(5);
  layout->setAlignment(Qt::AlignHCenter);
  layout->addWidget(button_, 0, Qt::AlignHCenter);
  label_->setAlignment(Qt::AlignCenter);
  label_->setObjectName(QStringLiteral("ControlLabel"));
  layout->addWidget(label_);
  connect(button_, &IconButton::clicked, this, &ControlButton::clicked);
  connect(button_, &IconButton::pressed, this, &ControlButton::pressed);
  connect(button_, &IconButton::released, this, &ControlButton::released);
}

void ControlButton::SetIcon(theme::Icon icon) { button_->SetIcon(icon); }
void ControlButton::SetText(const QString& text) { label_->setText(text); }
void ControlButton::SetTone(const QString& tone) { button_->SetTone(tone); }
void ControlButton::setEnabledState(bool enabled) { setEnabled(enabled); }

}  // namespace rtcwin::ui