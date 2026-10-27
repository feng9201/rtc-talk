#pragma once

#include "rtcwin/ui/theme/theme.h"

#include <QToolButton>

namespace rtcwin::ui {

class ControlButton final : public QToolButton {
  Q_OBJECT

 public:
  explicit ControlButton(theme::Icon icon, const QString& text,
                         QWidget* parent = nullptr);

  void SetIcon(theme::Icon icon);
  void SetText(const QString& text);
  void SetTone(const QString& tone);

 private:
  theme::Icon icon_;
};

}  // namespace rtcwin::ui
