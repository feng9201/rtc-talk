#include "rtcwin/ui/main_window.h"

#include "rtcwin/application/app_controller.h"
#include "rtcwin/client_gateway/c_abi_engine_client.h"
#include "rtcwin/ui/pages/prejoin_page.h"
#include "rtcwin/ui/pages/room_page.h"
#include "rtcwin/ui/widgets/window_controls.h"

#include <QResizeEvent>
#include <QStackedWidget>
#include <QVBoxLayout>

#include <utility>

namespace rtcwin::ui {
namespace {

domain::PeerState MakePeer(const std::string& uid, const std::string& name,
                           domain::RoomRole role, bool muted, bool camera,
                           double level) {
  domain::PeerState peer;
  peer.uid = uid;
  peer.display_name = name;
  peer.role = role;
  peer.audio_muted = muted;
  peer.camera_on = camera;
  peer.audio_level = level;
  return peer;
}

domain::SessionSnapshot PreviewState() {
  domain::SessionSnapshot state;
  state.connection = domain::ConnectionPhase::Ready;
  state.join = domain::JoinPhase::Joined;
  state.room_id = "preview-room-204";
  state.self_uid = "self";
  state.self_display_name = "我";
  state.self_role = domain::RoomRole::Host;
  state.selected_uid = "u2";
  state.media_mode = "sfu";
  state.media_enabled = true;
  state.mic_muted = false;
  state.camera_on = true;
  state.talk_mode = domain::TalkMode::Free;
  state.policy.talk_mode = domain::TalkMode::Free;
  state.peers.push_back(MakePeer("u1", "陈工", domain::RoomRole::Member,
                                 false, true, 0.62));
  state.peers.push_back(MakePeer("u2", "李工", domain::RoomRole::Member,
                                 true, true, 0.35));
  state.peers.push_back(MakePeer("u3", "王工", domain::RoomRole::Member,
                                 false, false, 0.18));
  state.peers.push_back(MakePeer("u4", "赵工", domain::RoomRole::Member,
                                 true, false, 0.10));
  state.peers.push_back(MakePeer("u5", "运维值班", domain::RoomRole::Member,
                                 false, true, 0.45));
  return state;
}

}  // namespace

MainWindow::MainWindow(QWidget* parent) : FramelessWindow(parent) {
  setObjectName(QStringLiteral("AppWindow"));
  setWindowTitle(QStringLiteral("RTC Talk"));
  BuildUi();
  ConnectSignals();
  prejoin_page_->SetOptions(controller_->settings().Load());
  ShowPrejoinPage();
}

MainWindow::~MainWindow() = default;

void MainWindow::BuildUi() {
  auto* layout = new QVBoxLayout(this);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);

  stack_ = new QStackedWidget(this);
  stack_->setObjectName(QStringLiteral("PageStack"));
  prejoin_page_ = new PrejoinPage(stack_);
  room_page_ = new RoomPage(stack_);
  controller_ = std::make_unique<application::AppController>(
      std::make_unique<client_gateway::CAbiEngineClient>());
  stack_->addWidget(prejoin_page_);
  stack_->addWidget(room_page_);
  layout->addWidget(stack_);

  window_controls_ = new WindowControls(this);
  window_controls_->raise();
  PositionWindowControls();
}

void MainWindow::ConnectSignals() {
  connect(window_controls_, &WindowControls::MinimizeRequested, this,
          &QWidget::showMinimized);
  connect(window_controls_, &WindowControls::MaximizeRequested, this,
          &MainWindow::ToggleMaximize);
  connect(window_controls_, &WindowControls::CloseRequested, this,
          &QWidget::close);

  connect(prejoin_page_, &PrejoinPage::JoinRequested, this,
          &MainWindow::BeginJoin);
  connect(prejoin_page_, &PrejoinPage::SaveRequested, this,
          [this](const domain::PrejoinOptions& options) {
            controller_->settings().Save(options);
            prejoin_page_->SetStatus(QStringLiteral("设置已保存"),
                                     QStringLiteral("ok"));
          });
  connect(prejoin_page_, &PrejoinPage::PreviewToggleRequested, this, [this] {
    prejoin_page_->SetStatus(
        QStringLiteral("muxPlayer 本地预览将在 Engine 接入后启用"),
        QStringLiteral("warn"));
  });

  connect(room_page_, &RoomPage::LeaveRequested, controller_.get(),
          &application::AppController::LeaveRoom);
  connect(room_page_, &RoomPage::MicToggleRequested, controller_.get(),
          &application::AppController::ToggleMic);
  connect(room_page_, &RoomPage::MicFloorPressed, controller_.get(),
          &application::AppController::RequestFloor);
  connect(room_page_, &RoomPage::MicFloorReleased, controller_.get(),
          &application::AppController::ReleaseFloor);
  connect(room_page_, &RoomPage::CameraToggleRequested, controller_.get(),
          &application::AppController::ToggleCamera);
  connect(room_page_, &RoomPage::ShareToggleRequested, controller_.get(),
          &application::AppController::ToggleShare);
  connect(room_page_, &RoomPage::DeafenToggleRequested, controller_.get(),
          &application::AppController::ToggleDeafened);
  connect(room_page_, &RoomPage::MemberSelected, this,
          [this](const QString& uid) {
            controller_->SelectPeer(uid.toStdString());
          });

  connect(controller_.get(), &application::AppController::SnapshotChanged,
          room_page_, &RoomPage::SetState);
  connect(controller_.get(), &application::AppController::Joined, this,
          &MainWindow::ShowRoomPage);
  connect(controller_.get(), &application::AppController::Left, this,
          [this] {
            ShowPrejoinPage();
            prejoin_page_->SetStatus(QStringLiteral("已离开房间"),
                                     QStringLiteral("info"));
          });
  connect(controller_.get(),
          &application::AppController::BusyChanged, this,
          [this](bool busy, const QString& message) {
            prejoin_page_->SetBusy(busy);
            room_page_->SetBusy(busy);
            if (!message.isEmpty()) {
              UpdateStatus(message, busy ? QStringLiteral("info")
                                         : QStringLiteral("default"));
            }
          });
  connect(controller_.get(), &application::AppController::ErrorOccurred,
          this, [this](const QString& message, const QString& detail) {
            UpdateStatus(detail.isEmpty() ? message
                                         : QStringLiteral("%1：%2")
                                               .arg(message, detail),
                         QStringLiteral("danger"));
          });
}

void MainWindow::BeginJoin(const domain::PrejoinOptions& options) {
  prejoin_page_->SetBusy(true);
  UpdateStatus(QStringLiteral("正在建立会话…"), QStringLiteral("info"));
  controller_->BeginJoin(options);
}

void MainWindow::ShowRoomPreview() {
  room_page_->SetPreviewMode(true);
  room_page_->SetState(PreviewState());
  stack_->setCurrentWidget(room_page_);
  window_controls_->raise();
}

void MainWindow::ShowPrejoinPage() {
  stack_->setCurrentWidget(prejoin_page_);
  window_controls_->raise();
}

void MainWindow::ShowRoomPage() {
  room_page_->SetState(controller_->snapshot());
  stack_->setCurrentWidget(room_page_);
  window_controls_->raise();
}

void MainWindow::UpdateStatus(const QString& text, const QString& tone) {
  prejoin_page_->SetStatus(text, tone);
}

void MainWindow::PositionWindowControls() {
  if (window_controls_ == nullptr) return;
  window_controls_->move(width() - window_controls_->width() - 18, 18);
}

void MainWindow::resizeEvent(QResizeEvent* event) {
  FramelessWindow::resizeEvent(event);
  PositionWindowControls();
}

}  // namespace rtcwin::ui
