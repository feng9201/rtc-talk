#pragma once

#include <QWidget>

namespace rtcwin::ui {

class FramelessWindow : public QWidget {
  Q_OBJECT

 public:
  explicit FramelessWindow(QWidget* parent = nullptr);

  void ToggleMaximize();

 protected:
  bool nativeEvent(const QByteArray& event_type, void* message,
                   long* result) override;
  void paintEvent(QPaintEvent* event) override;

 private:
  bool IsInteractiveAt(const QPoint& global_pos) const;
};

}  // namespace rtcwin::ui
