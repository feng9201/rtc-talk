#include "main_window.h"

#include "pages/prejoin_page.h"
#include "pages/room_page.h"
#include "services/app_settings_service.h"
#include "services/session_controller.h"
#include "widgets/toast.h"
#include "widgets/window_controls.h"

#include <QResizeEvent>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace talk::desktop {

MainWindow::MainWindow(QWidget* parent) : FramelessWindow(parent) {
  setObjectName(QStringLiteral("DesktopMainWindow"));
  setWindowTitle(QStringLiteral("RTC Talk"));
  settings_ = std::make_unique<AppSettingsService>();
  BuildUi();
  ConnectSignals();
  prejoin_page_->SetOptions(settings_->Load());
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
  controller_ = new SessionController(this);
  stack_->addWidget(prejoin_page_);
  stack_->addWidget(room_page_);
  layout->addWidget(stack_);

  window_controls_ = new WindowControls(this);
  PositionWindowControls();
  window_controls_->raise();
}

void MainWindow::ConnectSignals() {
  connect(prejoin_page_, &PrejoinPage::joinRequested, this,
          [this](const PrejoinOptions& options) {
            BeginJoin(options);
            emit joinRequested(options);
          });
  connect(prejoin_page_, &PrejoinPage::saveSettingsRequested, this,
          [this](const PrejoinOptions& options) {
            settings_->Save(options);
            UpdatePrejoinStatus(QStringLiteral("设置已保存"),
                                QStringLiteral("ok"));
          });
  connect(room_page_, &RoomPage::leaveRequested, controller_,
          &SessionController::LeaveRoom);
  connect(room_page_, &RoomPage::micToggleRequested, this,
          [this] {
            controller_->SetMicEnabled(controller_->snapshot().micMuted);
          });
  connect(room_page_, &RoomPage::micFloorPressed, controller_,
          &SessionController::RequestFloor);
  connect(room_page_, &RoomPage::micFloorReleased, controller_,
          &SessionController::ReleaseFloor);
  connect(room_page_, &RoomPage::cameraToggleRequested, controller_,
          &SessionController::ToggleCamera);
  connect(room_page_, &RoomPage::shareToggleRequested, controller_,
          &SessionController::ToggleShare);
  connect(room_page_, &RoomPage::deafenToggleRequested, controller_,
          &SessionController::ToggleDeafen);
  connect(room_page_, &RoomPage::memberSelected, controller_,
          &SessionController::SelectPeer);
  connect(room_page_, &RoomPage::grantFloorRequested, controller_,
          &SessionController::GrantFloor);
  connect(room_page_, &RoomPage::kickMemberRequested, controller_,
          &SessionController::KickMember);
  connect(room_page_, &RoomPage::muteAllRequested, controller_,
          &SessionController::MuteAll);
  connect(room_page_, &RoomPage::policyUpdateRequested, controller_,
          &SessionController::UpdatePolicy);

  connect(controller_, &SessionController::StateChanged, this,
          [this](const SessionSnapshot& state) {
            room_page_->SetSessionState(state);
          });
  connect(controller_, &SessionController::Joined, this,
          [this] {
            ShowRoomPage();
            Toast::Show(this, QStringLiteral("已进入房间"),
                        QStringLiteral("ok"));
          });
  connect(controller_, &SessionController::Left, this, [this] {
    ShowPrejoinPage();
    prejoin_page_->setJoinEnabled(true);
    UpdatePrejoinStatus(QStringLiteral("已离开房间"),
                        QStringLiteral("info"));
  });
  connect(controller_, &SessionController::Kicked, this,
          [this](const QString& reason) {
            ShowPrejoinPage();
            prejoin_page_->setJoinEnabled(true);
            const QString message =
                reason == QStringLiteral("replaced")
                    ? QStringLiteral("账号已在其他客户端登录")
                    : QStringLiteral("已被移出房间");
            UpdatePrejoinStatus(message, QStringLiteral("danger"));
          });
  connect(controller_, &SessionController::BusyChanged, this,
          [this](bool busy, const QString& message) {
            prejoin_page_->setJoinEnabled(!busy);
            room_page_->SetBusy(busy);
            if (!message.isEmpty()) {
              UpdatePrejoinStatus(message,
                                  busy ? QStringLiteral("info")
                                       : QStringLiteral("default"));
            }
          });
  connect(controller_, &SessionController::ErrorOccurred, this,
          [this](const QString& message, const QString& detail) {
            prejoin_page_->setJoinEnabled(true);
            const QString text =
                detail.isEmpty() ? message
                                 : QStringLiteral("%1：%2").arg(message, detail);
            UpdatePrejoinStatus(text, QStringLiteral("danger"));
            if (controller_->snapshot().joined) {
              Toast::Show(this, text, QStringLiteral("error"));
            }
          });
  connect(controller_, &SessionController::Notice, this,
          [this](const QString& message, const QString& tone) {
            Toast::Show(this, message, tone);
          });
}

void MainWindow::BeginJoin(const PrejoinOptions& options) {
  settings_->Save(options);
  prejoin_page_->setJoinEnabled(false);
  UpdatePrejoinStatus(QStringLiteral("正在建立会话…"),
                      QStringLiteral("info"));
  controller_->ConnectAndJoin(options);
}

void MainWindow::OpenRoomPolicyDialog() {
  if (controller_->snapshot().joined) room_page_->OpenPolicyDialog();
}

void MainWindow::ShowPrejoinPage() {
  stack_->setCurrentWidget(prejoin_page_);
  window_controls_->raise();
}

void MainWindow::ShowRoomPage() {
  room_page_->SetSessionState(controller_->snapshot());
  stack_->setCurrentWidget(room_page_);
  window_controls_->raise();
}

void MainWindow::UpdatePrejoinStatus(const QString& message,
                                     const QString& tone) {
  prejoin_page_->setStatusMessage(message, tone);
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
