#include "gl_play_widget.h"

#include "../display/soft_gl_display.h"
#include "../render/frame_pump.h"

#include <QCoreApplication>
#include <QMouseEvent>

GLPlayWidget::GLPlayWidget(QWidget* parent) : QOpenGLWidget(parent) {
  setUpdateBehavior(QOpenGLWidget::NoPartialUpdate);
  setMouseTracking(true);
}

GLPlayWidget::~GLPlayWidget() {
  makeCurrent();
  display_.reset();
  doneCurrent();
}

void GLPlayWidget::setSoftwareFramePump(muxplay::FramePump* pump) {
  softwareFramePump_ = pump;
  if (glInited_) {
    makeCurrent();
    recreateDisplay();
    doneCurrent();
  }
}

void GLPlayWidget::setDecodeMode(muxplay::DecodeMode mode) {
  if (decodeMode_ == mode) return;
  decodeMode_ = mode;
  if (glInited_) {
    makeCurrent();
    recreateDisplay();
    doneCurrent();
  }
}

void GLPlayWidget::requestNewFrame() {
  forceClear_ = false;
  QMetaObject::invokeMethod(this, [this]() { update(); }, Qt::QueuedConnection);
}

void GLPlayWidget::setKeepLastFrame(bool keep) { keepLastFrame_ = keep; }

void GLPlayWidget::clearFrame() {
  forceClear_ = true;
  update();
}

void GLPlayWidget::initializeGL() {
  initializeOpenGLFunctions();
  glInited_ = true;
  recreateDisplay();
}

void GLPlayWidget::recreateDisplay() {
  display_.reset();
  if (!softwareFramePump_ || decodeMode_ != muxplay::DecodeMode::Software) {
    return;
  }
  auto display = std::make_unique<muxplay::SoftGLDisplay>(
      [this]() -> AVFrame* {
        return softwareFramePump_ ? softwareFramePump_->acquireLatestFrame()
                                  : nullptr;
      });
  if (display->initGL(this)) display_ = std::move(display);
}

void GLPlayWidget::paintGL() {
  if (!glInited_ || !display_ || !display_->isReady()) {
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    return;
  }
  if (forceClear_) {
    display_->clear();
    return;
  }
  const qreal dpr = devicePixelRatioF();
  display_->paint(static_cast<int>(width() * dpr),
                  static_cast<int>(height() * dpr));
}

void GLPlayWidget::mousePressEvent(QMouseEvent* event) {
  event->ignore();
  if (auto* w = window()) {
    QMouseEvent e(event->type(), w->mapFromGlobal(event->globalPos()),
                  event->button(), event->buttons(), event->modifiers());
    QCoreApplication::sendEvent(w, &e);
  }
}
void GLPlayWidget::mouseMoveEvent(QMouseEvent* event) {
  event->ignore();
  if (auto* w = window()) {
    QMouseEvent e(event->type(), w->mapFromGlobal(event->globalPos()),
                  event->button(), event->buttons(), event->modifiers());
    QCoreApplication::sendEvent(w, &e);
  }
}
void GLPlayWidget::mouseReleaseEvent(QMouseEvent* event) {
  event->ignore();
  if (auto* w = window()) {
    QMouseEvent e(event->type(), w->mapFromGlobal(event->globalPos()),
                  event->button(), event->buttons(), event->modifiers());
    QCoreApplication::sendEvent(w, &e);
  }
}
void GLPlayWidget::mouseDoubleClickEvent(QMouseEvent* event) {
  event->ignore();
  if (auto* w = window()) {
    QMouseEvent e(event->type(), w->mapFromGlobal(event->globalPos()),
                  event->button(), event->buttons(), event->modifiers());
    QCoreApplication::sendEvent(w, &e);
  }
}
