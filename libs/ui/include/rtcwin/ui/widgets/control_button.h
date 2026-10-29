#pragma once

#include "rtcwin/ui/theme/theme.h"

#include <QWidget>

class QLabel;

namespace rtcwin::ui {

class IconButton;

class ControlButton final : public QWidget {
  Q_OBJECT

 public:
  explicit ControlButton(theme::Icon icon, const QString& text,
                         QWidget* parent = nullptr);

  void SetIcon(theme::Icon icon);
  void SetText(const QString& text);
  void SetTone(const QString& tone);
  void setEnabledState(bool enabled);
  IconButton* button() const { return button_; }

 signals:
  void clicked();
  void pressed();
  void released();

 private:
  IconButton* button_ = nullptr;
  QLabel* label_ = nullptr;
};

}  // namespace rtcwin::ui