#include "base/FramelessWindow.h"

#include <QAbstractButton>
#include <QAbstractSlider>
#include <QComboBox>
#include <QLineEdit>
#include <QPainter>
#include <QScrollBar>
#include <QWindow>

#ifdef Q_OS_WIN
#  include <windows.h>
#  include <windowsx.h>
#endif

FramelessWindow::FramelessWindow(QWidget *parent)
    : QWidget(parent)
{
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint | Qt::WindowMinMaxButtonsHint);
    setAttribute(Qt::WA_NativeWindow);
    setMouseTracking(true);
    setMinimumSize(1100, 720);
    resize(1440, 900);
}

void FramelessWindow::toggleMaximize()
{
    if (isMaximized()) showNormal();
    else showMaximized();
}

bool FramelessWindow::isInteractiveAt(const QPoint &globalPos) const
{
    const QWidget *child = childAt(mapFromGlobal(globalPos));
    while (child && child != this) {
        if (child->property("noWindowDrag").toBool())
            return true;
        if (child->inherits("QAbstractButton") || child->inherits("QLineEdit") ||
            child->inherits("QComboBox") || child->inherits("QAbstractSlider") ||
            child->inherits("QAbstractScrollArea") || child->inherits("QTextEdit") ||
            child->inherits("QPlainTextEdit"))
            return true;
        child = child->parentWidget();
    }
    return false;
}

bool FramelessWindow::nativeEvent(const QByteArray &eventType, void *message, long *result)
{
#ifdef Q_OS_WIN
    Q_UNUSED(eventType)
    MSG *msg = static_cast<MSG *>(message);

    if (!m_minBoxEnsured && msg->hwnd) {
        LONG_PTR style = GetWindowLongPtr(msg->hwnd, GWL_STYLE);
        SetWindowLongPtr(msg->hwnd, GWL_STYLE, style | WS_MINIMIZEBOX);
        SetWindowPos(msg->hwnd, nullptr, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
        m_minBoxEnsured = true;
    }

    if (msg->message == WM_NCHITTEST) {
        // WM_NCHITTEST receives physical pixels, while Qt's mapFromGlobal()
        // expects device-independent coordinates on scaled displays.
        const qreal dpr = devicePixelRatioF();
        const int gx = static_cast<int>(GET_X_LPARAM(msg->lParam) / dpr);
        const int gy = static_cast<int>(GET_Y_LPARAM(msg->lParam) / dpr);
        const QPoint globalPos(gx, gy);
        const QPoint localPos = mapFromGlobal(globalPos);
        const int border = qMax(5, qRound(borderWidth()));

        if (!isMaximized()) {
            if (localPos.x() <= border && localPos.y() <= border) { *result = HTTOPLEFT; return true; }
            if (localPos.x() >= width() - border && localPos.y() <= border) { *result = HTTOPRIGHT; return true; }
            if (localPos.x() <= border && localPos.y() >= height() - border) { *result = HTBOTTOMLEFT; return true; }
            if (localPos.x() >= width() - border && localPos.y() >= height() - border) { *result = HTBOTTOMRIGHT; return true; }
            if (localPos.x() <= border) { *result = HTLEFT; return true; }
            if (localPos.x() >= width() - border) { *result = HTRIGHT; return true; }
            if (localPos.y() <= border) { *result = HTTOP; return true; }
            if (localPos.y() >= height() - border) { *result = HTBOTTOM; return true; }
        }

        if (!isInteractiveAt(globalPos) && localPos.y() >= 0 && localPos.y() <= 72) {
            *result = HTCAPTION;
            return true;
        }
    }
#else
    Q_UNUSED(eventType)
    Q_UNUSED(message)
    Q_UNUSED(result)
#endif
    return QWidget::nativeEvent(eventType, message, result);
}

qreal FramelessWindow::borderWidth() const
{
    return 1.0;
}

void FramelessWindow::paintEvent(QPaintEvent *event)
{
    QWidget::paintEvent(event);
    if (isMaximized()) return;
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(QColor(QStringLiteral("#263442")), borderWidth()));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(rect().adjusted(0, 0, -1, -1));
}
