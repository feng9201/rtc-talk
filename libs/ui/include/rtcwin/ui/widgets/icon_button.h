#pragma once

#include "rtcwin/ui/theme/theme.h"

#include <QAbstractButton>

namespace rtcwin::ui {

class IconButton final : public QAbstractButton {
  Q_OBJECT

 public:
  explicit IconButton(theme::Icon icon, QWidget* parent = nullptr);

  void SetIcon(theme::Icon icon);
  void SetTone(const QString& tone);
  void SetDiameter(int diameter);
  QSize sizeHint() const override;

 protected:
  void paintEvent(QPaintEvent* event) override;
  void enterEvent(QEvent* event) override;
  void leaveEvent(QEvent* event) override;

 private:
  theme::Icon icon_;
  QString tone_ = QStringLiteral("default");
  int diameter_ = 42;
  bool hovered_ = false;
};

}  // namespace rtcwin::ui