#include "rtcwin/ui/base/frameless_window.h"

#include <QAbstractButton>
#include <QAbstractSlider>
#include <QComboBox>
#include <QLineEdit>
#include <QPainter>
#include <QScrollArea>
#include <QWindow>

#ifdef Q_OS_WIN
#include <windows.h>
#include <windowsx.h>
#endif

namespace rtcwin::ui {

FramelessWindow::FramelessWindow(QWidget* parent) : QWidget(parent) {
  setWindowFlags(Qt::Window | Qt::FramelessWindowHint |
                 Qt::WindowMinMaxButtonsHint);
  setAttribute(Qt::WA_NativeWindow);
  setAttribute(Qt::WA_StyledBackground, true);
  setMouseTracking(true);
  setMinimumSize(1100, 720);
  resize(1440, 900);
}

void FramelessWindow::ToggleMaximize() {
  if (isMaximized()) {
    showNormal();
  } else {
    showMaximized();
  }
}

bool FramelessWindow::IsInteractiveAt(const QPoint& global_pos) const {
  const QWidget* child = childAt(mapFromGlobal(global_pos));
  while (child != nullptr && child != this) {
    if (child->property("noWindowDrag").toBool()) return true;
    if (child->inherits("QAbstractButton") ||
        child->inherits("QLineEdit") ||
        child->inherits("QComboBox") ||
        child->inherits("QAbstractSlider") ||
        child->inherits("QAbstractScrollArea")) {
      return true;
    }
    child = child->parentWidget();
  }
  return false;
}

bool FramelessWindow::nativeEvent(const QByteArray& event_type, void* message,
                                  long* result) {
  Q_UNUSED(event_type);
#ifdef Q_OS_WIN
  auto* msg = static_cast<MSG*>(message);
  if (msg->message == WM_NCHITTEST) {
    const qreal ratio = devicePixelRatioF();
    const QPoint global_pos(
        static_cast<int>(GET_X_LPARAM(msg->lParam) / ratio),
        static_cast<int>(GET_Y_LPARAM(msg->lParam) / ratio));
    const QPoint local = mapFromGlobal(global_pos);
    const int border = qMax(5, qRound(5 * ratio));
    if (!isMaximized()) {
      if (local.x() <= border && local.y() <= border) {
        *result = HTTOPLEFT;
        return true;
      }
      if (local.x() >= width() - border && local.y() <= border) {
        *result = HTTOPRIGHT;
        return true;
      }
      if (local.x() <= border && local.y() >= height() - border) {
        *result = HTBOTTOMLEFT;
        return true;
      }
      if (local.x() >= width() - border && local.y() >= height() - border) {
        *result = HTBOTTOMRIGHT;
        return true;
      }
      if (local.x() <= border) {
        *result = HTLEFT;
        return true;
      }
      if (local.x() >= width() - border) {
        *result = HTRIGHT;
        return true;
      }
      if (local.y() <= border) {
        *result = HTTOP;
        return true;
      }
      if (local.y() >= height() - border) {
        *result = HTBOTTOM;
        return true;
      }
    }
    if (!IsInteractiveAt(global_pos) && local.y() <= 70) {
      *result = HTCAPTION;
      return true;
    }
  }
#else
  Q_UNUSED(message);
  Q_UNUSED(result);
#endif
  return QWidget::nativeEvent(event_type, message, result);
}

void FramelessWindow::paintEvent(QPaintEvent* event) {
  QWidget::paintEvent(event);
  if (isMaximized()) return;
  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing);
  painter.setPen(QPen(QColor(QStringLiteral("#263543")), 1));
  painter.setBrush(Qt::NoBrush);
  painter.drawRect(rect().adjusted(0, 0, -1, -1));
}

}  // namespace rtcwin::ui
