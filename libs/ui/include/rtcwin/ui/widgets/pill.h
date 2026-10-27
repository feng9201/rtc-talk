#pragma once

#include <QLabel>

namespace rtcwin::ui {

class Pill final : public QLabel {
  Q_OBJECT

 public:
  explicit Pill(const QString& text, const QString& tone = QStringLiteral("default"),
                QWidget* parent = nullptr);

  void SetTone(const QString& tone);
  void SetIcon(const QPixmap& icon);
};

}  // namespace rtcwin::ui
