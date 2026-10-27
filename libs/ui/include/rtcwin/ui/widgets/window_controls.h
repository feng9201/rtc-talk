#pragma once

#include <QWidget>

namespace rtcwin::ui {

class WindowControls final : public QWidget {
  Q_OBJECT

 public:
  explicit WindowControls(QWidget* parent = nullptr);

 signals:
  void MinimizeRequested();
  void MaximizeRequested();
  void CloseRequested();
};

}  // namespace rtcwin::ui
