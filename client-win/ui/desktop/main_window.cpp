#include "main_window.h"

#include "pages/prejoin_page.h"
#include "pages/room_page.h"
#include "widgets/window_controls.h"

#include <QResizeEvent>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace talk::desktop {

MainWindow::MainWindow(QWidget* parent) : FramelessWindow(parent) {
  setObjectName(QStringLiteral("DesktopMainWindow"));
  setWindowTitle(QStringLiteral("RTC Talk"));
  BuildUi();
  ConnectSignals();
  ShowPrejoinPage();
}

void MainWindow::BuildUi() {
  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);

  stack_ = new QStackedWidget(this);
  stack_->setObjectName(QStringLiteral("PageStack"));
  prejoin_page_ = new PrejoinPage(stack_);
  room_page_ = new RoomPage(stack_);
  stack_->addWidget(prejoin_page_);
  stack_->addWidget(room_page_);
  layout->addWidget(stack_);

  window_controls_ = new WindowControls(this);
  PositionWindowControls();
  window_controls_->raise();
}

void MainWindow::ConnectSignals() {
  connect(prejoin_page_, &PrejoinPage::joinRequested, this,
          &MainWindow::joinRequested);
  connect(room_page_, &RoomPage::leaveRequested, this,
          &MainWindow::ShowPrejoinPage);
}

void MainWindow::ShowPrejoinPage() {
  stack_->setCurrentWidget(prejoin_page_);
  window_controls_->raise();
}

void MainWindow::ShowRoomPreview() {
  room_page_->SetPreviewMode(true);
  stack_->setCurrentWidget(room_page_);
  window_controls_->raise();
}

void MainWindow::SetLogPath(const QString& path) {
  log_path_ = path;
}

void MainWindow::PositionWindowControls() {
  if (window_controls_ == nullptr) return;
  window_controls_->move(width() - window_controls_->width() - 18, 18);
}

void MainWindow::resizeEvent(QResizeEvent* event) {
  FramelessWindow::resizeEvent(event);
  PositionWindowControls();
}

}  // namespace talk::desktop
