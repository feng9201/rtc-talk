#pragma once

#include <QWidget>

namespace rtcwin::ui {

class Pill final : public QWidget {
  Q_OBJECT

 public:
  explicit Pill(const QString& text = {},
                const QString& tone = QStringLiteral("default"),
                QWidget* parent = nullptr);

  void setText(const QString& text);
  void SetTone(const QString& tone);
  QSize sizeHint() const override;

 protected:
  void paintEvent(QPaintEvent* event) override;

 private:
  QString text_;
  QString tone_;
};

}  // namespace rtcwin::ui