#pragma once

#include "rtcwin/domain/types.h"

#include <QHash>
#include <QWidget>

class QLabel;
class QPushButton;
class QScrollArea;
class QVBoxLayout;

namespace rtcwin::ui {

class ControlButton;
class MemberRow;
class Pill;
class SegmentedControl;
class VideoGrid;

class RoomPage final : public QWidget {
  Q_OBJECT

 public:
  explicit RoomPage(QWidget* parent = nullptr);

  void SetState(const domain::SessionSnapshot& state);
  void SetVideoFrame(const domain::VideoFrame& frame);
  void SetPreviewMode(bool preview);
  void SetBusy(bool busy);
  void SetControlHint(const QString& text);
  const domain::SessionSnapshot& state() const { return state_; }

 signals:
  void MicToggleRequested();
  void MicFloorPressed();
  void MicFloorReleased();
  void CameraToggleRequested();
  void ShareToggleRequested();
  void DeafenToggleRequested();
  void SettingsRequested();
  void LeaveRequested();
  void MemberSelected(const QString& uid);
  void GrantFloorRequested(const QString& uid, bool allow);
  void KickMemberRequested(const QString& uid);
  void MuteAllRequested();

 private:
  QWidget* BuildHeader();
  QWidget* BuildStage();
  QWidget* BuildControlBar();
  QWidget* BuildSidebar();
  void UpdateHeader();
  void UpdateControls();
  void RebuildSidePanel();
  void RefreshMembers();
  void UpdateMemberPanel();
  domain::PeerState SelfPeer() const;

  domain::SessionSnapshot state_;
  bool preview_mode_ = false;
  bool busy_ = false;
  bool joined_at_set_ = false;
  qint64 joined_at_ms_ = 0;

  QLabel* room_title_ = nullptr;
  QLabel* room_id_ = nullptr;
  Pill* connection_ = nullptr;
  Pill* role_ = nullptr;
  Pill* media_mode_ = nullptr;
  Pill* member_count_ = nullptr;
  Pill* clock_ = nullptr;
  Pill* floor_ = nullptr;
  SegmentedControl* filter_ = nullptr;
  SegmentedControl* layout_ = nullptr;
  SegmentedControl* tabs_ = nullptr;
  VideoGrid* video_grid_ = nullptr;
  ControlButton* mic_ = nullptr;
  ControlButton* camera_ = nullptr;
  ControlButton* share_ = nullptr;
  ControlButton* listen_ = nullptr;
  ControlButton* settings_ = nullptr;
  ControlButton* leave_ = nullptr;
  QLabel* control_hint_ = nullptr;
  QScrollArea* side_scroll_ = nullptr;
  QWidget* side_body_ = nullptr;
  QVBoxLayout* side_layout_ = nullptr;
  QLabel* member_summary_ = nullptr;
  QLabel* member_floor_ = nullptr;
  QPushButton* mute_all_ = nullptr;
  QVBoxLayout* member_rows_layout_ = nullptr;
  QHash<QString, MemberRow*> member_rows_;
  QStringList member_order_;
};

}  // namespace rtcwin::ui
