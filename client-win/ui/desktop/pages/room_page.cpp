#include "pages/room_page.h"

#include "dialogs/room_policy_dialog.h"
#include "theme/theme.h"
#include "widgets/control_button.h"
#include "widgets/member_row.h"
#include "widgets/pill.h"
#include "widgets/segmented_control.h"

#include <QDateTime>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QStyle>
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

  floor_timer_ = new QTimer(this);
  floor_timer_->setInterval(1000);
  connect(floor_timer_, &QTimer::timeout, this, [this] {
    UpdateHeader();
    if (tabs_ != nullptr && tabs_->currentId() == QStringLiteral("members")) {
      UpdateMemberPanel();
    }
  });
  floor_timer_->start();
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
      QStringLiteral("控制操作会直接作用于当前房间"),
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

  connect(mic_, &ControlButton::clicked, this, [this] {
    if (state_.talkMode == TalkMode::Free) emit micToggleRequested();
  });
  connect(mic_, &ControlButton::pressed, this, [this] {
    if (state_.talkMode != TalkMode::Free) emit micFloorPressed();
  });
  connect(mic_, &ControlButton::released, this, [this] {
    if (state_.talkMode != TalkMode::Free) emit micFloorReleased();
  });
  connect(camera_, &ControlButton::clicked, this,
          &RoomPage::cameraToggleRequested);
  connect(share_, &ControlButton::clicked, this,
          &RoomPage::shareToggleRequested);
  connect(listen_, &ControlButton::clicked, this,
          &RoomPage::deafenToggleRequested);
  connect(settings_, &ControlButton::clicked, this,
          &RoomPage::OpenPolicyDialog);
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

void RoomPage::SetSessionState(const SessionSnapshot& state) {
  state_ = state;
  UpdateHeader();
  UpdateControls();
  RefreshSidePanel();
}

void RoomPage::SetPreviewMode(bool preview) {
  preview_mode_ = preview;
  UpdateHeader();
  UpdateControls();
  RefreshSidePanel();
}

void RoomPage::SetBusy(bool busy) {
  busy_ = busy;
  UpdateControls();
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
  if (state_.talkMode == TalkMode::Free) {
    floor_->setText(QStringLiteral("自由发言"));
    floor_->setTone(QStringLiteral("default"));
  } else if (state_.floor.holderUid.isEmpty()) {
    floor_->setText(QStringLiteral("等待发言"));
    floor_->setTone(QStringLiteral("default"));
  } else {
    const QString holder =
        state_.floor.selfHasFloor ? QStringLiteral("我") : state_.floor.holderUid;
    const qint64 left =
        state_.floor.deadlineEpochMs > 0
            ? qMax<qint64>(0, state_.floor.deadlineEpochMs -
                                  QDateTime::currentMSecsSinceEpoch())
            : 0;
    floor_->setText(left > 0
                        ? QStringLiteral("发言：%1 · %2s")
                              .arg(holder)
                              .arg((left + 999) / 1000)
                        : QStringLiteral("发言：%1").arg(holder));
    floor_->setTone(QStringLiteral("ok"));
  }
  if (preview_mode_ && !state_.joined) {
    room_title_->setText(QStringLiteral("房间页面预览"));
    room_id_->setText(QStringLiteral("P1 架构骨架"));
    connection_->setText(QStringLiteral("预览模式"));
    connection_->setTone(QStringLiteral("accent"));
  }
}

void RoomPage::UpdateControls() {
  const bool joined = state_.joined;
  const bool media = joined && state_.mediaEnabled;
  const bool host = state_.selfRole == QStringLiteral("host");
  if (mic_ != nullptr) mic_->setEnabledState(joined && !busy_);
  if (camera_ != nullptr) camera_->setEnabledState(media && !busy_);
  if (share_ != nullptr) share_->setEnabledState(media && !busy_);
  if (listen_ != nullptr) listen_->setEnabledState(media && !busy_);
  if (settings_ != nullptr) settings_->setEnabledState(joined && host && !busy_);
  if (leave_ != nullptr) leave_->setEnabledState(joined && !busy_);

  if (mic_ != nullptr) {
    if (state_.talkMode == TalkMode::Free) {
      mic_->setIcon(state_.micMuted ? Theme::Icon::MicOff : Theme::Icon::Mic);
      mic_->setText(state_.micMuted ? QStringLiteral("闭麦")
                                    : QStringLiteral("麦克风"));
      mic_->setTone(state_.micMuted ? QStringLiteral("muted")
                                    : QStringLiteral("active"));
    } else {
      mic_->setIcon(state_.floor.selfHasFloor ? Theme::Icon::Mic
                                              : Theme::Icon::MicOff);
      mic_->setText(
          state_.floor.selfHasFloor
              ? QStringLiteral("正在发言")
              : state_.talkMode == TalkMode::Ptt ? QStringLiteral("按住说话")
                                                 : QStringLiteral("申请发言"));
      mic_->setTone(state_.floor.selfHasFloor ? QStringLiteral("active")
                                              : QStringLiteral("default"));
    }
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
  if (control_hint_ != nullptr) {
    if (preview_mode_) {
      control_hint_->setText(QStringLiteral("房间页面生产骨架预览"));
    } else if (state_.talkMode == TalkMode::Free) {
      control_hint_->setText(
          QStringLiteral("自由发言 · 点击麦克风切换闭麦状态"));
    } else if (!state_.floor.holderUid.isEmpty()) {
      const int queue_pos = state_.floor.queue.indexOf(state_.selfUid);
      control_hint_->setText(
          queue_pos >= 0
              ? QStringLiteral("发言权排队中，第 %1 位").arg(queue_pos + 1)
              : QStringLiteral("发言中：%1").arg(state_.floor.holderUid));
    } else {
      control_hint_->setText(
          state_.talkMode == TalkMode::Ptt
              ? QStringLiteral("按住麦克风申请并占用发言权")
              : QStringLiteral("按住麦克风申请发言，等待主持人放行"));
    }
  }
  if (filter_ != nullptr) filter_->setEnabled(false);
  if (layout_ != nullptr) layout_->setEnabled(false);
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
  member_panel_ = nullptr;
  member_summary_ = nullptr;
  member_floor_ = nullptr;
  mute_all_ = nullptr;
  member_rows_layout_ = nullptr;
  member_rows_.clear();
  member_order_.clear();

  const QString tab = tabs_ != nullptr ? tabs_->currentId()
                                        : QStringLiteral("members");
  if (tab != QStringLiteral("members")) {
    side_layout_->addWidget(
        Label(tab == QStringLiteral("devices") ? QStringLiteral("设备面板")
                                               : QStringLiteral("连接与媒体"),
              QStringLiteral("PanelHead"), side_body_));
    auto* placeholder = Theme::card(this, QStringLiteral("SubtleCard"));
    auto* placeholder_layout = new QVBoxLayout(placeholder);
    placeholder_layout->setContentsMargins(16, 18, 16, 18);
    placeholder_layout->addWidget(
        Label(tab == QStringLiteral("devices")
                  ? QStringLiteral("设备管理将在 P5/P6 完善")
                  : QStringLiteral("媒体统计将在 P6 接入"),
              QStringLiteral("TitleSmall"), placeholder));
    placeholder_layout->addWidget(
        Label(QStringLiteral("当前模块边界已预留"), QStringLiteral("Hint"),
              placeholder));
    side_layout_->addWidget(placeholder);
    side_layout_->addStretch(1);
    return;
  }

  member_panel_ = new QWidget(side_body_);
  auto* panel_layout = new QVBoxLayout(member_panel_);
  panel_layout->setContentsMargins(0, 0, 0, 0);
  panel_layout->setSpacing(8);

  auto* head = new QWidget(member_panel_);
  auto* head_layout = new QHBoxLayout(head);
  head_layout->setContentsMargins(0, 0, 0, 0);
  member_summary_ = Label(QString(), QStringLiteral("PanelHead"), head);
  head_layout->addWidget(member_summary_, 1);
  mute_all_ = Theme::button(QStringLiteral("全员静音"),
                            QStringLiteral("danger"), head);
  mute_all_->setMinimumHeight(30);
  head_layout->addWidget(mute_all_);
  panel_layout->addWidget(head);

  member_floor_ = Label(QString(), QStringLiteral("FloorLine"), member_panel_);
  panel_layout->addWidget(member_floor_);

  auto* rows = new QWidget(member_panel_);
  member_rows_layout_ = new QVBoxLayout(rows);
  member_rows_layout_->setContentsMargins(0, 0, 0, 0);
  member_rows_layout_->setSpacing(7);
  panel_layout->addWidget(rows);
  side_layout_->addWidget(member_panel_);
  side_layout_->addStretch(1);

  connect(mute_all_, &QPushButton::clicked, this, &RoomPage::muteAllRequested);
  UpdateMemberPanel();
}

void RoomPage::RefreshSidePanel() {
  if (tabs_ == nullptr || tabs_->currentId() != QStringLiteral("members")) {
    RebuildSidePanel();
    return;
  }

  QStringList ids;
  ids << state_.selfUid;
  for (const PeerSnapshot& peer : state_.peers) ids << peer.uid;
  bool same_ids = ids.size() == member_order_.size();
  if (same_ids) {
    for (int i = 0; i < ids.size(); ++i) {
      if (ids.at(i) != member_order_.at(i)) {
        same_ids = false;
        break;
      }
    }
  }
  if (member_panel_ == nullptr || !same_ids) {
    RebuildSidePanel();
  } else {
    UpdateMemberPanel();
  }
}

PeerSnapshot RoomPage::SelfPeer() const {
  PeerSnapshot self;
  self.uid = state_.selfUid;
  self.displayName = state_.selfDisplayName;
  self.role = state_.selfRole;
  self.audioMuted = state_.micMuted;
  self.speaking = state_.floor.selfHasFloor;
  self.cameraOn = state_.cameraOn;
  self.screenOn = state_.sharing;
  self.audioLevel = state_.selfAudioLevel;
  return self;
}

void RoomPage::UpdateMemberPanel() {
  if (member_panel_ == nullptr || member_rows_layout_ == nullptr) return;
  const bool host = state_.selfRole == QStringLiteral("host");
  member_summary_->setText(
      QStringLiteral("房间成员 · %1").arg(state_.peers.size() + 1));
  if (mute_all_ != nullptr) mute_all_->setVisible(host);

  QString floor_text;
  if (state_.talkMode == TalkMode::Free) {
    floor_text = QStringLiteral("自由发言（全双工）");
  } else if (state_.floor.holderUid.isEmpty()) {
    floor_text = QStringLiteral("发言权：未授予");
  } else {
    floor_text = QStringLiteral("发言权：%1")
                     .arg(state_.floor.selfHasFloor ? QStringLiteral("我")
                                                    : state_.floor.holderUid);
    if (!state_.floor.queue.isEmpty()) {
      floor_text += QStringLiteral(" · 排队 %1 人")
                        .arg(state_.floor.queue.size());
    }
  }
  member_floor_->setText(floor_text);

  if (member_rows_layout_->count() == 0) {
    member_order_.clear();
    const PeerSnapshot self = SelfPeer();
    member_order_ << self.uid;
    auto* self_row = new MemberRow(member_panel_);
    member_rows_.insert(self.uid, self_row);
    member_rows_layout_->addWidget(self_row);
    connect(self_row, &MemberRow::memberSelected, this,
            &RoomPage::memberSelected);
    for (const PeerSnapshot& peer : state_.peers) {
      member_order_ << peer.uid;
      auto* row = new MemberRow(member_panel_);
      member_rows_.insert(peer.uid, row);
      member_rows_layout_->addWidget(row);
      connect(row, &MemberRow::memberSelected, this,
              &RoomPage::memberSelected);
      connect(row, &MemberRow::grantRequested, this,
              &RoomPage::grantFloorRequested);
      connect(row, &MemberRow::kickRequested, this,
              &RoomPage::kickMemberRequested);
    }
  }

  const PeerSnapshot self = SelfPeer();
  for (auto it = member_rows_.begin(); it != member_rows_.end(); ++it) {
    const bool is_self = it.key() == state_.selfUid;
    PeerSnapshot peer = is_self ? self : PeerSnapshot{};
    if (!is_self) {
      for (const PeerSnapshot& candidate : state_.peers) {
        if (candidate.uid == it.key()) {
          peer = candidate;
          break;
        }
      }
    }
    it.value()->SetMember(peer, is_self, state_.selfRole, host,
                          state_.talkMode, state_.floor,
                          state_.selectedUid == peer.uid);
  }
}

void RoomPage::OpenPolicyDialog() {
  if (state_.selfRole != QStringLiteral("host")) return;
  if (policy_dialog_ == nullptr) {
    policy_dialog_ = new RoomPolicyDialog(this);
  }
  policy_dialog_->SetPolicy(state_.policy);
  emit settingsRequested();
  if (policy_dialog_->exec() == QDialog::Accepted) {
    emit policyUpdateRequested(policy_dialog_->policy());
  }
}

void RoomPage::resizeEvent(QResizeEvent* event) {
  QWidget::resizeEvent(event);
  if (control_hint_ != nullptr) {
    control_hint_->setVisible(width() >= 1240);
  }
}

}  // namespace talk::desktop
