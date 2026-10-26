#include "pages/room_page.h"

#include "theme/theme.h"
#include "widgets/control_button.h"
#include "widgets/pill.h"
#include "widgets/segmented_control.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QResizeEvent>
#include <QScrollArea>
#include <QTimer>
#include <QVBoxLayout>

namespace talk::desktop {
namespace {

QLabel* Label(const QString& text, const QString& object_name,
              QWidget* parent = nullptr) {
  return Theme::label(text, object_name, parent);
}

QString ConnectionLabel(ConnectionState state) {
  switch (state) {
    case ConnectionState::Connecting:
      return QStringLiteral("连接中");
    case ConnectionState::Ready:
      return QStringLiteral("已连接");
    case ConnectionState::Reconnecting:
      return QStringLiteral("重连中");
    case ConnectionState::Failed:
      return QStringLiteral("连接失败");
    case ConnectionState::Disconnected:
      return QStringLiteral("未连接");
  }
  return QStringLiteral("未连接");
}

}  // namespace

RoomPage::RoomPage(QWidget* parent) : QWidget(parent) {
  setObjectName(QStringLiteral("RoomPage"));
  setAttribute(Qt::WA_StyledBackground, true);

  auto* root = new QHBoxLayout(this);
  root->setContentsMargins(0, 0, 0, 0);
  root->setSpacing(0);

  auto* main = new QWidget(this);
  main->setObjectName(QStringLiteral("RoomMain"));
  auto* main_layout = new QVBoxLayout(main);
  main_layout->setContentsMargins(0, 0, 0, 0);
  main_layout->setSpacing(0);
  main_layout->addWidget(BuildHeader());
  main_layout->addWidget(BuildStage(), 1);
  main_layout->addWidget(BuildControlBar());
  root->addWidget(main, 1);
  root->addWidget(BuildSidebar());

  UpdateHeader();
  UpdateControls();
  RebuildSidePanel();
}

QWidget* RoomPage::BuildHeader() {
  auto* header = new QFrame(this);
  header->setObjectName(QStringLiteral("RoomHeader"));
  header->setFixedHeight(64);
  auto* layout = new QHBoxLayout(header);
  layout->setContentsMargins(22, 0, 126, 0);
  layout->setSpacing(10);

  auto* titles = new QVBoxLayout;
  titles->setContentsMargins(0, 0, 0, 0);
  titles->setSpacing(2);
  auto* title_line = new QHBoxLayout;
  title_line->setContentsMargins(0, 0, 0, 0);
  title_line->setSpacing(8);
  room_title_ = Label(QStringLiteral("尚未加入房间"),
                      QStringLiteral("RoomTitle"), header);
  room_id_ = Label(QStringLiteral("生产客户端骨架"),
                   QStringLiteral("RoomId"), header);
  title_line->addWidget(room_title_);
  title_line->addWidget(room_id_);
  title_line->addStretch(1);
  titles->addLayout(title_line);

  auto* badges = new QHBoxLayout;
  badges->setContentsMargins(0, 0, 0, 0);
  badges->setSpacing(7);
  connection_ = new Pill(QStringLiteral("未连接"),
                         QStringLiteral("warn"), header);
  role_ = new Pill(QStringLiteral("成员"), QStringLiteral("strong"), header);
  badges->addWidget(connection_);
  badges->addWidget(role_);
  badges->addStretch(1);
  titles->addLayout(badges);
  layout->addLayout(titles, 1);

  media_mode_ = new Pill(QStringLiteral("模式 —"),
                         QStringLiteral("default"), header);
  member_count_ = new Pill(QStringLiteral("0 人"),
                           QStringLiteral("default"), header);
  member_count_->setIcon(Theme::Icon::Users);
  clock_ = new Pill(QStringLiteral("00:00:00"),
                    QStringLiteral("default"), header);
  clock_->setIcon(Theme::Icon::Clock);
  layout->addWidget(media_mode_);
  layout->addWidget(member_count_);
  layout->addWidget(clock_);
  return header;
}

QWidget* RoomPage::BuildStage() {
  auto* stage = new QWidget(this);
  stage->setObjectName(QStringLiteral("RoomStage"));
  auto* layout = new QVBoxLayout(stage);
  layout->setContentsMargins(14, 10, 14, 12);
  layout->setSpacing(10);

  auto* bar = new QFrame(stage);
  bar->setObjectName(QStringLiteral("StageBar"));
  bar->setFixedHeight(44);
  auto* bar_layout = new QHBoxLayout(bar);
  bar_layout->setContentsMargins(12, 0, 12, 0);
  bar_layout->setSpacing(10);
  filter_ = new SegmentedControl(true, bar);
  filter_->addItem(QStringLiteral("all"), QStringLiteral("全部成员"));
  filter_->addItem(QStringLiteral("video"), QStringLiteral("仅视频成员"));
  filter_->setEnabled(false);
  bar_layout->addWidget(filter_);
  bar_layout->addStretch(1);
  floor_ = new Pill(QStringLiteral("等待入房"), QStringLiteral("default"), bar);
  bar_layout->addWidget(floor_);
  layout_ = new SegmentedControl(true, bar);
  layout_->addItem(QStringLiteral("grid"), QStringLiteral("宫格"));
  layout_->addItem(QStringLiteral("focus"), QStringLiteral("聚焦"));
  layout_->setEnabled(false);
  bar_layout->addWidget(layout_);
  layout->addWidget(bar);

  auto* empty = new QFrame(stage);
  empty->setObjectName(QStringLiteral("StageEmpty"));
  auto* empty_layout = new QVBoxLayout(empty);
  empty_layout->setAlignment(Qt::AlignCenter);
  empty_layout->setSpacing(10);
  auto* icon = new QLabel(empty);
  icon->setPixmap(Theme::pixmap(Theme::Icon::CameraOff, 46,
                                QColor(QStringLiteral("#5c6b79"))));
  icon->setAlignment(Qt::AlignCenter);
  empty_layout->addWidget(icon);
  stage_empty_ = Label(QStringLiteral("核心接入后显示真实远端视频"),
                       QStringLiteral("Muted"), empty);
  stage_empty_->setAlignment(Qt::AlignCenter);
  empty_layout->addWidget(stage_empty_);
  empty_layout->addWidget(
      Label(QStringLiteral("P5 将接入 mid 级视频流注册表和渲染 Surface"),
            QStringLiteral("Hint"), empty),
      0, Qt::AlignHCenter);
  layout->addWidget(empty, 1);
  return stage;
}

QWidget* RoomPage::BuildControlBar() {
  auto* bar = new QFrame(this);
  bar->setObjectName(QStringLiteral("ControlBar"));
  bar->setProperty("noWindowDrag", true);
  bar->setFixedHeight(92);
  auto* layout = new QHBoxLayout(bar);
  layout->setContentsMargins(24, 9, 24, 9);
  layout->setSpacing(12);

  mic_ = new ControlButton(Theme::Icon::MicOff, QStringLiteral("闭麦"), bar);
  camera_ = new ControlButton(Theme::Icon::CameraOff,
                              QStringLiteral("摄像头"), bar);
  share_ = new ControlButton(Theme::Icon::Screen,
                             QStringLiteral("共享屏幕"), bar);
  layout->addWidget(mic_);
  layout->addWidget(camera_);
  layout->addWidget(share_);
  layout->addStretch(1);
  control_hint_ = Label(
      QStringLiteral("P1 生产界面骨架 · 控制命令将在 P4 接入"),
      QStringLiteral("ControlHint"), bar);
  layout->addWidget(control_hint_);
  layout->addStretch(1);
  listen_ = new ControlButton(Theme::Icon::Headphones,
                              QStringLiteral("收听"), bar);
  settings_ = new ControlButton(Theme::Icon::Settings,
                                QStringLiteral("设置"), bar);
  leave_ = new ControlButton(Theme::Icon::Leave, QStringLiteral("离开"), bar);
  leave_->setTone(QStringLiteral("danger"));
  layout->addWidget(listen_);
  layout->addWidget(settings_);
  layout->addWidget(leave_);

  connect(mic_, &ControlButton::clicked, this, &RoomPage::micToggleRequested);
  connect(camera_, &ControlButton::clicked, this,
          &RoomPage::cameraToggleRequested);
  connect(share_, &ControlButton::clicked, this,
          &RoomPage::shareToggleRequested);
  connect(listen_, &ControlButton::clicked, this,
          &RoomPage::deafenToggleRequested);
  connect(settings_, &ControlButton::clicked, this,
          &RoomPage::settingsRequested);
  connect(leave_, &ControlButton::clicked, this, &RoomPage::leaveRequested);
  return bar;
}

QWidget* RoomPage::BuildSidebar() {
  auto* side = new QFrame(this);
  side->setObjectName(QStringLiteral("RoomSidebar"));
  side->setFixedWidth(322);
  auto* layout = new QVBoxLayout(side);
  layout->setContentsMargins(0, 0, 0, 0);
  layout->setSpacing(0);

  auto* head = new QFrame(side);
  head->setObjectName(QStringLiteral("SideHead"));
  head->setFixedHeight(58);
  auto* head_layout = new QHBoxLayout(head);
  head_layout->setContentsMargins(14, 10, 126, 8);
  tabs_ = new SegmentedControl(false, head);
  tabs_->addItem(QStringLiteral("members"), QStringLiteral("成员"));
  tabs_->addItem(QStringLiteral("devices"), QStringLiteral("设备"));
  tabs_->addItem(QStringLiteral("status"), QStringLiteral("状态"));
  head_layout->addWidget(tabs_, 1);
  layout->addWidget(head);

  side_scroll_ = new QScrollArea(side);
  side_scroll_->setObjectName(QStringLiteral("RoomSideScroll"));
  side_scroll_->setWidgetResizable(true);
  side_scroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  side_body_ = new QWidget(side_scroll_);
  side_body_->setObjectName(QStringLiteral("SideBody"));
  side_layout_ = new QVBoxLayout(side_body_);
  side_layout_->setContentsMargins(14, 12, 14, 14);
  side_layout_->setSpacing(10);
  side_scroll_->setWidget(side_body_);
  layout->addWidget(side_scroll_, 1);

  connect(tabs_, &SegmentedControl::currentIdChanged, this,
          [this](const QString&) {
            RebuildSidePanel();
            const QString id = tabs_->currentId();
            emit panelTabRequested(
                id == QStringLiteral("devices")
                    ? PanelTab::Devices
                    : id == QStringLiteral("status") ? PanelTab::Status
                                                      : PanelTab::Members);
          });
  return side;
}

QWidget* RoomPage::BuildMemberPlaceholder() {
  auto* frame = Theme::card(this, QStringLiteral("SubtleCard"));
  auto* layout = new QVBoxLayout(frame);
  layout->setContentsMargins(16, 18, 16, 18);
  layout->setSpacing(8);
  layout->addWidget(Label(QStringLiteral("尚无房间成员"),
                          QStringLiteral("TitleSmall"), frame));
  layout->addWidget(Label(
      QStringLiteral("P2 接入真实 SessionStore 后显示成员、角色和音量状态。"),
      QStringLiteral("Hint"), frame));
  return frame;
}

void RoomPage::SetSessionState(const SessionSnapshot& state) {
  state_ = state;
  UpdateHeader();
  UpdateControls();
  RebuildSidePanel();
}

void RoomPage::SetPreviewMode(bool preview) {
  preview_mode_ = preview;
  UpdateHeader();
  UpdateControls();
  RebuildSidePanel();
}

void RoomPage::UpdateHeader() {
  if (room_title_ == nullptr) return;
  room_title_->setText(state_.roomId.isEmpty() ? QStringLiteral("尚未加入房间")
                                                : QStringLiteral("房间会话"));
  room_id_->setText(state_.roomId.isEmpty() ? QStringLiteral("生产客户端骨架")
                                             : state_.roomId);
  connection_->setText(ConnectionLabel(state_.connectionState));
  connection_->setTone(state_.connectionState == ConnectionState::Ready
                           ? QStringLiteral("ok")
                           : state_.connectionState == ConnectionState::Failed
                                 ? QStringLiteral("danger")
                                 : QStringLiteral("warn"));
  role_->setText(state_.selfRole == QStringLiteral("host")
                     ? QStringLiteral("主持人")
                     : QStringLiteral("成员"));
  role_->setVisible(state_.joined);
  media_mode_->setText(state_.mediaMode.isEmpty()
                           ? QStringLiteral("模式 —")
                           : state_.mediaMode.toUpper());
  member_count_->setText(QStringLiteral("%1 人").arg(state_.peers.size() + 1));
  clock_->setText(QStringLiteral("00:00:00"));
  floor_->setText(state_.talkMode == TalkMode::Free
                      ? QStringLiteral("自由发言")
                      : state_.floor.holderUid.isEmpty()
                            ? QStringLiteral("等待发言")
                            : QStringLiteral("发言中"));
  if (preview_mode_ && !state_.joined) {
    room_title_->setText(QStringLiteral("房间页面预览"));
    room_id_->setText(QStringLiteral("P1 架构骨架"));
    connection_->setText(QStringLiteral("预览模式"));
    connection_->setTone(QStringLiteral("accent"));
  }
}

void RoomPage::UpdateControls() {
  const bool joined = state_.joined || preview_mode_;
  for (ControlButton* control : {mic_, camera_, share_, listen_, settings_,
                                 leave_}) {
    if (control != nullptr) control->setEnabledState(joined);
  }
  if (mic_ != nullptr) {
    mic_->setIcon(state_.micMuted ? Theme::Icon::MicOff : Theme::Icon::Mic);
    mic_->setText(state_.micMuted ? QStringLiteral("闭麦")
                                  : QStringLiteral("麦克风"));
    mic_->setTone(state_.micMuted ? QStringLiteral("muted")
                                  : QStringLiteral("active"));
  }
  if (camera_ != nullptr) {
    camera_->setIcon(state_.cameraOn ? Theme::Icon::Camera
                                     : Theme::Icon::CameraOff);
    camera_->setText(state_.cameraOn ? QStringLiteral("摄像头")
                                     : QStringLiteral("摄像头关闭"));
    camera_->setTone(state_.cameraOn ? QStringLiteral("active")
                                     : QStringLiteral("muted"));
  }
  if (share_ != nullptr) {
    share_->setText(state_.sharing ? QStringLiteral("停止共享")
                                   : QStringLiteral("共享屏幕"));
    share_->setTone(state_.sharing ? QStringLiteral("active")
                                   : QStringLiteral("default"));
  }
  if (listen_ != nullptr) {
    listen_->setIcon(state_.deafen ? Theme::Icon::HeadphonesOff
                                   : Theme::Icon::Headphones);
    listen_->setText(state_.deafen ? QStringLiteral("已静音")
                                   : QStringLiteral("收听"));
  }
  if (filter_ != nullptr) filter_->setEnabled(joined);
  if (layout_ != nullptr) layout_->setEnabled(joined && !state_.sharing);
  if (stage_empty_ != nullptr) {
    stage_empty_->setText(preview_mode_
                              ? QStringLiteral("房间页面预览")
                              : QStringLiteral("核心接入后显示真实远端视频"));
  }
}

void RoomPage::RebuildSidePanel() {
  if (side_layout_ == nullptr) return;
  while (QLayoutItem* item = side_layout_->takeAt(0)) {
    if (QWidget* widget = item->widget()) widget->deleteLater();
    if (QLayout* child = item->layout()) {
      while (QLayoutItem* child_item = child->takeAt(0)) {
        if (QWidget* widget = child_item->widget()) widget->deleteLater();
        delete child_item;
      }
      delete child;
    }
    delete item;
  }

  const QString tab = tabs_ != nullptr ? tabs_->currentId()
                                        : QStringLiteral("members");
  side_layout_->addWidget(
      Label(tab == QStringLiteral("devices") ? QStringLiteral("设备面板")
            : tab == QStringLiteral("status") ? QStringLiteral("连接与媒体")
                                              : QStringLiteral("房间成员"),
            QStringLiteral("PanelHead"), side_body_));
  side_layout_->addWidget(BuildMemberPlaceholder());
  side_layout_->addStretch(1);
}

void RoomPage::resizeEvent(QResizeEvent* event) {
  QWidget::resizeEvent(event);
  if (control_hint_ != nullptr) {
    control_hint_->setVisible(width() >= 1240);
  }
}

}  // namespace talk::desktop
