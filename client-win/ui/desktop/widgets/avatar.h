#pragma once

#include <QWidget>

namespace talk::desktop {

class Avatar final : public QWidget {
  Q_OBJECT

 public:
  explicit Avatar(QWidget* parent = nullptr);
  Avatar(const QString& text, int diameter, QWidget* parent = nullptr);

  void SetText(const QString& text);
  void SetDiameter(int diameter);
  QSize sizeHint() const override;

 protected:
  void paintEvent(QPaintEvent* event) override;

 private:
  QString text_;
  int diameter_ = 40;
};

}  // namespace talk::desktop
