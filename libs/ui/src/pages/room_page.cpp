#include "rtcwin/ui/pages/room_page.h"

#include "rtcwin/ui/theme/theme.h"
#include "rtcwin/ui/video/video_grid.h"
#include "rtcwin/ui/widgets/control_button.h"
#include "rtcwin/ui/widgets/device_panel.h"
#include "rtcwin/ui/widgets/member_row.h"
#include "rtcwin/ui/widgets/pill.h"
#include "rtcwin/ui/widgets/segmented_control.h"

#include <QDateTime>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QScrollArea>
#include <QStyle>
#include <QStringList>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <iterator>

namespace rtcwin::ui {
namespace {

QString Text(const std::string& value) {
  return QString::fromStdString(value);
}

QString ConnectionLabel(domain::ConnectionPhase phase) {
  switch (phase) {
    case domain::ConnectionPhase::Connecting:
      return QStringLiteral("连接中");
    case domain::ConnectionPhase::Ready:
      return QStringLiteral("已连接");
    case domain::ConnectionPhase::Reconnecting:
      return QStringLiteral("重连中");
    case domain::ConnectionPhase::Failed:
      return QStringLiteral("连接失败");
    case domain::ConnectionPhase::Disconnected:
      return QStringLiteral("未连接");
  }
  return QStringLiteral("未连接");
}

}  // namespace

RoomPage::RoomPage(QWidget* parent) : QWidget(parent) {
  setObjectName(QStringLiteral("RoomPage"));
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

  auto* timer = new QTimer(this);
  timer->setInterval(1000);
  connect(timer, &QTimer::timeout, this, [this] {
    UpdateHeader();
    if (tabs_->CurrentId() == QStringLiteral("members")) UpdateMemberPanel();
    if (tabs_->CurrentId() == QStringLiteral("status")) UpdateStatusPanel();
  });
  timer->start();
  UpdateHeader();
  UpdateControls();
  RebuildSidePanel();
}

QWidget* RoomPage::BuildHeader() {
  auto* header = new QFrame(this);
  header->setObjectName(QStringLiteral("RoomHeader"));
  header->setFixedHeight(64);
  auto* layout = new QHBoxLayout(header);
  layout->setContentsMargins(22, 0, 132, 0);
  layout->setSpacing(10);

  auto* titles = new QVBoxLayout;
  titles->setSpacing(2);
  auto* title_line = new QHBoxLayout;
  title_line->setSpacing(8);
  room_title_ = theme::Label(QStringLiteral("尚未加入房间"),
                             QStringLiteral("RoomTitle"), header);
  room_id_ = theme::Label(QStringLiteral("生产房间"),
                          QStringLiteral("RoomId"), header);
  title_line->addWidget(room_title_);
  title_line->addWidget(room_id_);
  title_line->addStretch(1);
  titles->addLayout(title_line);

  auto* badges = new QHBoxLayout;
  badges->setSpacing(7);
  connection_ = new Pill(QStringLiteral("未连接"),
                         QStringLiteral("warn"), header);
  role_ = new Pill(QStringLiteral("成员"), QStringLiteral("default"), header);
  badges->addWidget(connection_);
  badges->addWidget(role_);
  badges->addStretch(1);
  titles->addLayout(badges);
  layout->addLayout(titles, 1);

  media_mode_ = new Pill(QStringLiteral("模式 —"),
                         QStringLiteral("default"), header);
  member_count_ = new Pill(QStringLiteral("0 人"),
                           QStringLiteral("default"), header);
  clock_ = new Pill(QStringLiteral("00:00:00"),
                    QStringLiteral("default"), header);
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
  filter_->AddItem(QStringLiteral("all"), QStringLiteral("全部成员"));
  filter_->AddItem(QStringLiteral("video"), QStringLiteral("仅视频成员"));
  bar_layout->addWidget(filter_);
  bar_layout->addStretch(1);
  floor_ = new Pill(QStringLiteral("等待入房"),
                    QStringLiteral("default"), bar);
  bar_layout->addWidget(floor_);
  layout_ = new SegmentedControl(true, bar);
  layout_->AddItem(QStringLiteral("grid"), QStringLiteral("宫格"));
  layout_->AddItem(QStringLiteral("focus"), QStringLiteral("聚焦"));
  bar_layout->addWidget(layout_);
  layout->addWidget(bar);

  video_grid_ = new VideoGrid(stage);
  layout->addWidget(video_grid_, 1);

  connect(filter_, &SegmentedControl::CurrentIdChanged, this,
          [this](const QString& id) {
            video_grid_->SetFilter(id == QStringLiteral("video")
                                       ? domain::StageFilter::VideoOnly
                                       : domain::StageFilter::All);
          });
  connect(layout_, &SegmentedControl::CurrentIdChanged, this,
          [this](const QString& id) {
            video_grid_->SetLayoutMode(id == QStringLiteral("focus")
                                           ? domain::StageLayout::Focus
                                           : domain::StageLayout::Grid);
          });
  connect(video_grid_, &VideoGrid::MemberSelected, this,
          &RoomPage::MemberSelected);
  connect(video_grid_, &VideoGrid::MemberFocusRequested, this,
          [this](const QString& uid) {
            layout_->SetCurrentId(QStringLiteral("focus"));
            video_grid_->SetLayoutMode(domain::StageLayout::Focus);
            emit MemberSelected(uid);
          });
  return stage;
}

QWidget* RoomPage::BuildControlBar() {
  auto* bar = new QFrame(this);
  bar->setObjectName(QStringLiteral("ControlBar"));
  bar->setFixedHeight(92);
  auto* layout = new QHBoxLayout(bar);
  layout->setContentsMargins(24, 9, 24, 9);
  layout->setSpacing(12);

  mic_ = new ControlButton(theme::Icon::MicOff, QStringLiteral("闭麦"), bar);
  camera_ = new ControlButton(theme::Icon::CameraOff,
                              QStringLiteral("摄像头"), bar);
  share_ = new ControlButton(theme::Icon::Screen,
                             QStringLiteral("共享屏幕"), bar);
  layout->addWidget(mic_);
  layout->addWidget(camera_);
  layout->addWidget(share_);
  layout->addStretch(1);
  control_hint_ = theme::Label(QStringLiteral("控制操作会直接作用于当前房间"),
                               QStringLiteral("ControlHint"), bar);
  layout->addWidget(control_hint_);
  layout->addStretch(1);
  listen_ = new ControlButton(theme::Icon::Headphones,
                              QStringLiteral("收听"), bar);
  settings_ = new ControlButton(theme::Icon::Settings,
                                QStringLiteral("设置"), bar);
  leave_ = new ControlButton(theme::Icon::Leave,
                             QStringLiteral("离开"), bar);
  leave_->SetTone(QStringLiteral("danger"));
  layout->addWidget(listen_);
  layout->addWidget(settings_);
  layout->addWidget(leave_);

  connect(mic_, &ControlButton::clicked, this, [this] {
    if (state_.talk_mode == domain::TalkMode::Free) {
      emit MicToggleRequested();
    } else if (state_.talk_mode == domain::TalkMode::Moderated) {
      emit MicFloorToggleRequested();
    }
  });
  connect(mic_, &ControlButton::pressed, this, [this] {
    if (state_.talk_mode == domain::TalkMode::Ptt) emit MicFloorPressed();
  });
  connect(mic_, &ControlButton::released, this, [this] {
    if (state_.talk_mode == domain::TalkMode::Ptt) emit MicFloorReleased();
  });
  connect(camera_, &ControlButton::clicked, this,
          &RoomPage::CameraToggleRequested);
  connect(share_, &ControlButton::clicked, this,
          &RoomPage::ShareToggleRequested);
  connect(listen_, &ControlButton::clicked, this,
          &RoomPage::DeafenToggleRequested);
  connect(settings_, &ControlButton::clicked, this,
          &RoomPage::SettingsRequested);
  connect(leave_, &ControlButton::clicked, this, &RoomPage::LeaveRequested);
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
  head_layout->setContentsMargins(14, 10, 132, 8);
  tabs_ = new SegmentedControl(false, head);
  tabs_->AddItem(QStringLiteral("members"), QStringLiteral("成员"));
  tabs_->AddItem(QStringLiteral("devices"), QStringLiteral("设备"));
  tabs_->AddItem(QStringLiteral("status"), QStringLiteral("状态"));
  head_layout->addWidget(tabs_, 1);
  layout->addWidget(head);

  side_scroll_ = new QScrollArea(side);
  side_scroll_->setWidgetResizable(true);
  side_scroll_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  side_body_ = new QWidget(side_scroll_);
  side_layout_ = new QVBoxLayout(side_body_);
  side_layout_->setContentsMargins(14, 12, 14, 14);
  side_layout_->setSpacing(10);
  side_scroll_->setWidget(side_body_);
  layout->addWidget(side_scroll_, 1);

  connect(tabs_, &SegmentedControl::CurrentIdChanged, this,
          [this](const QString&) { RebuildSidePanel(); });
  return side;
}

void RoomPage::SetState(const domain::SessionSnapshot& state) {
  const bool was_joined = state_.join == domain::JoinPhase::Joined;
  state_ = state;
  const bool joined = state_.join == domain::JoinPhase::Joined;
  if (joined && !was_joined) {
    joined_at_ms_ = QDateTime::currentMSecsSinceEpoch();
    joined_at_set_ = true;
  } else if (!joined) {
    joined_at_set_ = false;
    joined_at_ms_ = 0;
  }
  video_grid_->SetState(state_);
  UpdateHeader();
  UpdateControls();
  RefreshMembers();
  UpdateStatusPanel();
  if (device_panel_ != nullptr) {
    device_panel_->SetSelection(state_.devices);
    device_panel_->SetSystemAudioEnabled(
        state_.audio_source == domain::AudioSource::System);
  }
}

void RoomPage::SetVideoFrame(const domain::VideoFrame& frame) {
  if (video_grid_ != nullptr) video_grid_->SetFrame(frame);
}

void RoomPage::SetPreviewMode(bool preview) {
  preview_mode_ = preview;
  UpdateHeader();
  UpdateControls();
}

void RoomPage::SelectSideTab(domain::PanelTab tab) {
  if (tabs_ == nullptr) return;
  switch (tab) {
    case domain::PanelTab::Members:
      tabs_->SetCurrentId(QStringLiteral("members"));
      break;
    case domain::PanelTab::Devices:
      tabs_->SetCurrentId(QStringLiteral("devices"));
      break;
    case domain::PanelTab::Status:
      tabs_->SetCurrentId(QStringLiteral("status"));
      break;
  }
  RebuildSidePanel();
}

void RoomPage::SetControlHint(const QString& text) {
  if (control_hint_ != nullptr) control_hint_->setText(text);
}

void RoomPage::SetCameraDevices(
    const QVector<rtcwin::domain::CameraDevice>& devices) {
  camera_devices_ = devices;
  if (device_panel_ != nullptr) {
    device_panel_->SetDevices(recording_devices_, playout_devices_,
                              camera_devices_, desktop_sources_);
    device_panel_->SetSelection(state_.devices);
    device_panel_->SetSystemAudioEnabled(
        state_.audio_source == domain::AudioSource::System);
  }
}

void RoomPage::SetAudioDevices(
    const QVector<rtcwin::domain::AudioDevice>& recording,
    const QVector<rtcwin::domain::AudioDevice>& playout) {
  recording_devices_ = recording;
  playout_devices_ = playout;
  if (device_panel_ != nullptr) {
    device_panel_->SetDevices(recording_devices_, playout_devices_,
                              camera_devices_, desktop_sources_);
    device_panel_->SetSelection(state_.devices);
  }
}

void RoomPage::SetDesktopSources(
    const QVector<rtcwin::domain::DesktopSource>& sources) {
  desktop_sources_ = sources;
  if (device_panel_ != nullptr) {
    device_panel_->SetDevices(recording_devices_, playout_devices_,
                              camera_devices_, desktop_sources_);
    device_panel_->SetSelection(state_.devices);
  }
}

void RoomPage::SetBusy(bool busy) {
  busy_ = busy;
  UpdateControls();
}

void RoomPage::UpdateHeader() {
  if (room_title_ == nullptr) return;
  room_title_->setText(state_.room_id.empty()
                           ? QStringLiteral("尚未加入房间")
                           : QStringLiteral("房间会话"));
  room_id_->setText(state_.room_id.empty() ? QStringLiteral("生产房间")
                                           : Text(state_.room_id));
  connection_->setText(ConnectionLabel(state_.connection));
  connection_->SetTone(
      state_.connection == domain::ConnectionPhase::Ready
          ? QStringLiteral("ok")
          : state_.connection == domain::ConnectionPhase::Failed
                ? QStringLiteral("danger")
                : QStringLiteral("warn"));
  role_->setText(state_.self_role == domain::RoomRole::Host
                     ? QStringLiteral("主持人")
                     : QStringLiteral("成员"));
  role_->setVisible(state_.join == domain::JoinPhase::Joined);
  media_mode_->setText(state_.media_mode.empty()
                           ? QStringLiteral("模式 —")
                           : Text(state_.media_mode).toUpper());
  member_count_->setText(QStringLiteral("%1 人")
                             .arg(static_cast<int>(state_.peers.size()) + 1));
  const qint64 elapsed =
      joined_at_set_ ? (QDateTime::currentMSecsSinceEpoch() - joined_at_ms_) / 1000
                     : 0;
  clock_->setText(theme::FormatDuration(elapsed));
  if (state_.talk_mode == domain::TalkMode::Free) {
    floor_->setText(QStringLiteral("自由发言"));
    floor_->SetTone(QStringLiteral("default"));
  } else if (state_.floor.holder_uid.empty()) {
    const int queue_position = QueuePosition();
    floor_->setText(queue_position > 0
                        ? QStringLiteral("排队中 · 第%1位").arg(queue_position)
                        : QStringLiteral("等待发言"));
    floor_->SetTone(QStringLiteral("default"));
  } else {
    const QString holder = state_.floor.self_has_floor
                               ? QStringLiteral("我")
                               : Text(state_.floor.holder_uid);
    QString text = QStringLiteral("发言：%1").arg(holder);
    if (state_.floor.deadline_epoch_ms > 0) {
      const qint64 remaining =
          std::max<qint64>(0, state_.floor.deadline_epoch_ms -
                                 QDateTime::currentMSecsSinceEpoch());
      text += QStringLiteral(" · %1s").arg((remaining + 999) / 1000);
    }
    const int queue_position = QueuePosition();
    if (queue_position > 0) {
      text += QStringLiteral(" · 排队第%1位").arg(queue_position);
    }
    floor_->setText(text);
    floor_->SetTone(QStringLiteral("ok"));
  }
  if (preview_mode_) {
    room_title_->setText(QStringLiteral("房间页面预览"));
    room_id_->setText(QStringLiteral("6 人布局"));
    connection_->setText(QStringLiteral("预览模式"));
    connection_->SetTone(QStringLiteral("accent"));
  }
}

void RoomPage::UpdateControls() {
  const bool joined = state_.join == domain::JoinPhase::Joined;
  const bool media = joined && state_.media_enabled;
  const bool video_enabled = media && state_.policy.video_enabled;
  if (mic_ != nullptr) mic_->setEnabled(joined && !busy_);
  if (camera_ != nullptr) camera_->setEnabled(video_enabled && !busy_);
  if (share_ != nullptr) share_->setEnabled(video_enabled && !busy_);
  if (listen_ != nullptr) listen_->setEnabled(media && !busy_);
  if (settings_ != nullptr) {
    settings_->setEnabled(joined &&
                          state_.self_role == domain::RoomRole::Host &&
                          !busy_);
  }
  if (leave_ != nullptr) leave_->setEnabled(joined && !busy_);
  if (filter_ != nullptr) filter_->SetEnabled(joined || preview_mode_);
  if (layout_ != nullptr) layout_->SetEnabled(joined || preview_mode_);
  if (mic_ != nullptr) {
    if (state_.talk_mode == domain::TalkMode::Free) {
      mic_->SetIcon(state_.mic_muted ? theme::Icon::MicOff
                                     : theme::Icon::Mic);
      mic_->SetText(state_.mic_muted ? QStringLiteral("闭麦")
                                     : QStringLiteral("麦克风"));
      mic_->SetTone(state_.mic_muted ? QStringLiteral("muted")
                                     : QStringLiteral("active"));
    } else {
      const bool queued = QueuePosition() > 0;
      mic_->SetIcon(state_.floor.self_has_floor ? theme::Icon::Mic
                                                : theme::Icon::MicOff);
      mic_->SetText(state_.floor.self_has_floor
                        ? QStringLiteral("正在发言")
                        : state_.talk_mode == domain::TalkMode::Ptt
                              ? QStringLiteral("按住说话")
                              : queued ? QStringLiteral("取消排队")
                                       : QStringLiteral("申请发言"));
      mic_->SetTone(state_.floor.self_has_floor
                        ? QStringLiteral("active")
                        : queued ? QStringLiteral("warn")
                                 : QStringLiteral("default"));
    }
  }
  if (camera_ != nullptr) {
    camera_->SetIcon(state_.camera_on ? theme::Icon::Camera
                                      : theme::Icon::CameraOff);
    camera_->SetText(state_.camera_on ? QStringLiteral("摄像头")
                                      : QStringLiteral("摄像头关闭"));
    camera_->SetTone(state_.camera_on ? QStringLiteral("active")
                                      : QStringLiteral("muted"));
  }
  if (share_ != nullptr) {
    share_->SetText(state_.sharing ? QStringLiteral("停止共享")
                                   : QStringLiteral("共享屏幕"));
    share_->SetTone(state_.sharing ? QStringLiteral("active")
                                   : QStringLiteral("default"));
  }
  if (listen_ != nullptr) {
    listen_->SetIcon(state_.deafened ? theme::Icon::HeadphonesOff
                                     : theme::Icon::Headphones);
    listen_->SetText(state_.deafened ? QStringLiteral("已静音")
                                     : QStringLiteral("收听"));
  }
  if (control_hint_ != nullptr) {
    control_hint_->setText(
        state_.talk_mode == domain::TalkMode::Free
            ? QStringLiteral("自由发言 · 点击麦克风切换闭麦状态")
            : state_.talk_mode == domain::TalkMode::Ptt
                  ? QStringLiteral("按住麦克风申请发言，松开释放")
                  : QStringLiteral("点击麦克风申请发言，再次点击取消或释放"));
  }
}

void RoomPage::RebuildSidePanel() {
  if (side_layout_ == nullptr) return;
  while (QLayoutItem* item = side_layout_->takeAt(0)) {
    if (QWidget* widget = item->widget()) widget->deleteLater();
    delete item;
  }
  member_rows_.clear();
  member_order_.clear();
  member_summary_ = nullptr;
  member_floor_ = nullptr;
  mute_all_ = nullptr;
  member_rows_layout_ = nullptr;
  device_panel_ = nullptr;
  status_values_.clear();

  const QString tab = tabs_->CurrentId();
  if (tab == QStringLiteral("devices")) {
    side_layout_->addWidget(theme::Label(QStringLiteral("设备与画面源"),
                                         QStringLiteral("PanelHead"),
                                         side_body_));
    device_panel_ = new DevicePanel(side_body_);
    side_layout_->addWidget(device_panel_);
    device_panel_->SetDevices(recording_devices_, playout_devices_,
                              camera_devices_, desktop_sources_);
    device_panel_->SetSelection(state_.devices);
    connect(device_panel_, &DevicePanel::RefreshRequested, this,
            &RoomPage::RefreshDeviceInventoryRequested);
    connect(device_panel_, &DevicePanel::RecordingDeviceSelected, this,
            &RoomPage::RecordingDeviceSelected);
    connect(device_panel_, &DevicePanel::PlayoutDeviceSelected, this,
            &RoomPage::PlayoutDeviceSelected);
    connect(device_panel_, &DevicePanel::SystemAudioDeviceSelected, this,
            &RoomPage::SystemAudioDeviceSelected);
    connect(device_panel_, &DevicePanel::CameraSelected, this,
            &RoomPage::CameraSelected);
    connect(device_panel_, &DevicePanel::DesktopSourceSelected, this,
            &RoomPage::DesktopSourceSelected);
    side_layout_->addStretch(1);
    return;
  }
  if (tab == QStringLiteral("status")) {
    side_layout_->addWidget(theme::Label(QStringLiteral("连接与媒体"),
                                         QStringLiteral("PanelHead"),
                                         side_body_));
    auto* card = theme::Card(side_body_, QStringLiteral("SubtleCard"));
    auto* card_layout = new QVBoxLayout(card);
    card_layout->setContentsMargins(16, 16, 16, 16);
    card_layout->setSpacing(8);
    const auto add_row = [this, card, card_layout](const QString& name,
                                                   const QString& value) {
      auto* row = new QHBoxLayout;
      row->addWidget(theme::Label(name, QStringLiteral("Muted"), card), 1);
      auto* text = theme::Label(value, QStringLiteral("Value"), card);
      text->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
      row->addWidget(text);
      card_layout->addLayout(row);
      status_values_.insert(name, text);
    };
    add_row(QStringLiteral("连接"),
            ConnectionLabel(state_.connection));
    add_row(QStringLiteral("媒体模式"),
            state_.media_mode.empty() ? QStringLiteral("—")
                                      : Text(state_.media_mode).toUpper());
    add_row(QStringLiteral("麦克风"),
            state_.mic_muted ? QStringLiteral("闭麦")
                             : QStringLiteral("开麦"));
    add_row(QStringLiteral("摄像头"),
            state_.camera_on ? QStringLiteral("开启")
                             : QStringLiteral("关闭"));
    add_row(QStringLiteral("屏幕共享"),
            state_.sharing ? QStringLiteral("进行中")
                           : QStringLiteral("关闭"));
    add_row(QStringLiteral("发言权"),
            state_.floor.self_has_floor
                ? QStringLiteral("本人")
                : state_.floor.holder_uid.empty()
                      ? QStringLiteral("未授予")
                      : Text(state_.floor.holder_uid));
    UpdateStatusPanel();
    side_layout_->addWidget(card);
    side_layout_->addStretch(1);
    return;
  }
  if (tab != QStringLiteral("members")) {
    side_layout_->addWidget(theme::Label(
        tab == QStringLiteral("devices") ? QStringLiteral("设备面板")
                                         : QStringLiteral("连接与媒体"),
        QStringLiteral("PanelHead"), side_body_));
    auto* card = theme::Card(side_body_, QStringLiteral("SubtleCard"));
    auto* card_layout = new QVBoxLayout(card);
    card_layout->setContentsMargins(16, 18, 16, 18);
    card_layout->addWidget(theme::Label(
        tab == QStringLiteral("devices") ? QStringLiteral("等待媒体引擎枚举设备")
                                         : QStringLiteral("等待媒体统计接入"),
        QStringLiteral("TitleSmall"), card));
    card_layout->addWidget(theme::Label(QStringLiteral("模块边界已建立"),
                                        QStringLiteral("Hint"), card));
    side_layout_->addWidget(card);
    side_layout_->addStretch(1);
    return;
  }

  auto* panel = new QWidget(side_body_);
  auto* panel_layout = new QVBoxLayout(panel);
  panel_layout->setContentsMargins(0, 0, 0, 0);
  panel_layout->setSpacing(8);

  auto* head = new QWidget(panel);
  auto* head_layout = new QHBoxLayout(head);
  head_layout->setContentsMargins(0, 0, 0, 0);
  member_summary_ = theme::Label(QString(), QStringLiteral("PanelHead"), head);
  head_layout->addWidget(member_summary_, 1);
  mute_all_ = theme::Button(QStringLiteral("全员静音"),
                            QStringLiteral("danger"), head);
  mute_all_->setMinimumHeight(30);
  head_layout->addWidget(mute_all_);
  panel_layout->addWidget(head);

  member_floor_ = theme::Label(QString(), QStringLiteral("FloorLine"), panel);
  panel_layout->addWidget(member_floor_);
  auto* rows = new QWidget(panel);
  member_rows_layout_ = new QVBoxLayout(rows);
  member_rows_layout_->setContentsMargins(0, 0, 0, 0);
  member_rows_layout_->setSpacing(7);
  panel_layout->addWidget(rows);
  side_layout_->addWidget(panel);
  side_layout_->addStretch(1);

  connect(mute_all_, &QPushButton::clicked, this,
          &RoomPage::MuteAllRequested);

  const auto self = SelfPeer();
  member_order_ << Text(self.uid);
  auto* self_row = new MemberRow(panel);
  member_rows_.insert(Text(self.uid), self_row);
  member_rows_layout_->addWidget(self_row);
  connect(self_row, &MemberRow::Selected, this, &RoomPage::MemberSelected);
  for (const auto& peer : state_.peers) {
    const QString uid = Text(peer.uid);
    member_order_ << uid;
    auto* row = new MemberRow(panel);
    member_rows_.insert(uid, row);
    member_rows_layout_->addWidget(row);
    connect(row, &MemberRow::Selected, this, &RoomPage::MemberSelected);
    connect(row, &MemberRow::GrantRequested, this,
            &RoomPage::GrantFloorRequested);
    connect(row, &MemberRow::KickRequested, this,
            &RoomPage::KickMemberRequested);
  }
  UpdateMemberPanel();
}

void RoomPage::RefreshMembers() {
  QStringList ids;
  const auto self = SelfPeer();
  ids << Text(self.uid);
  for (const auto& peer : state_.peers) ids << Text(peer.uid);
  if (ids != member_order_ || member_rows_.isEmpty()) {
    RebuildSidePanel();
  } else {
    UpdateMemberPanel();
  }
}

void RoomPage::UpdateMemberPanel() {
  if (member_summary_ == nullptr || member_rows_layout_ == nullptr) return;
  member_summary_->setText(QStringLiteral("房间成员 · %1")
                               .arg(static_cast<int>(state_.peers.size()) + 1));
  const bool host = state_.self_role == domain::RoomRole::Host;
  mute_all_->setVisible(host);
  member_floor_->setText(state_.floor.holder_uid.empty()
                             ? (QueuePosition() > 0
                                    ? QStringLiteral("发言权：排队第%1位")
                                          .arg(QueuePosition())
                                    : QStringLiteral("发言权：未授予"))
                             : QStringLiteral("发言权：%1")
                                   .arg(Text(state_.floor.holder_uid)));
  const auto self = SelfPeer();
  if (auto* row = member_rows_.value(Text(self.uid))) {
    row->SetMember(self, true, false, false,
                   Text(state_.selected_uid) == Text(self.uid));
  }
  for (const auto& peer : state_.peers) {
    if (auto* row = member_rows_.value(Text(peer.uid))) {
      row->SetMember(peer, false, host,
                     host && state_.talk_mode != domain::TalkMode::Free,
                     Text(state_.selected_uid) == Text(peer.uid));
    }
  }
}

void RoomPage::UpdateStatusPanel() {
  if (status_values_.isEmpty()) return;
  const auto set = [this](const QString& key, const QString& value) {
    if (auto* label = status_values_.value(key)) label->setText(value);
  };
  set(QStringLiteral("连接"), ConnectionLabel(state_.connection));
  set(QStringLiteral("媒体模式"),
      state_.media_mode.empty() ? QStringLiteral("—")
                                : Text(state_.media_mode).toUpper());
  set(QStringLiteral("麦克风"),
      state_.mic_muted ? QStringLiteral("闭麦") : QStringLiteral("开麦"));
  set(QStringLiteral("摄像头"),
      state_.camera_on ? QStringLiteral("开启") : QStringLiteral("关闭"));
  set(QStringLiteral("屏幕共享"),
      state_.sharing ? QStringLiteral("进行中") : QStringLiteral("关闭"));
  set(QStringLiteral("发言权"),
      state_.floor.self_has_floor
          ? QStringLiteral("本人")
          : state_.floor.holder_uid.empty()
                ? QStringLiteral("未授予")
                : Text(state_.floor.holder_uid));
}

int RoomPage::QueuePosition() const {
  const auto it = std::find(state_.floor.queue.begin(),
                            state_.floor.queue.end(),
                            state_.self_uid);
  if (it == state_.floor.queue.end()) return 0;
  return static_cast<int>(
             std::distance(state_.floor.queue.begin(), it)) +
         1;
}

domain::PeerState RoomPage::SelfPeer() const {
  domain::PeerState self;
  self.uid = state_.self_uid;
  self.display_name = state_.self_display_name.empty()
                          ? state_.self_uid
                          : state_.self_display_name;
  self.role = state_.self_role;
  self.audio_muted = state_.mic_muted;
  self.speaking = state_.floor.self_has_floor;
  self.camera_on = state_.camera_on;
  self.screen_on = state_.sharing;
  self.audio_level = state_.self_audio_level;
  return self;
}

}  // namespace rtcwin::ui
