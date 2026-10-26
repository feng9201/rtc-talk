#pragma once

#include <QFrame>

class QLabel;

namespace talk::desktop {

class Toast final : public QFrame {
  Q_OBJECT

 public:
  static void Show(QWidget* parent, const QString& text,
                   const QString& tone = QStringLiteral("info"));

 private:
  Toast(QWidget* parent, const QString& text, const QString& tone);
  void Reposition();

  QLabel* label_ = nullptr;
};

}  // namespace talk::desktop
