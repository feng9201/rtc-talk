#pragma once

#include "domain/app_types.h"
#include "state/video_stream_registry.h"

#include <QHash>
#include <QWidget>

class QLabel;
class QPushButton;
class QScrollArea;
class QTimer;
class QVBoxLayout;

namespace talk::desktop {

class ControlButton;
class IconButton;
class MemberRow;
class Pill;
class RoomPolicyDialog;
class SegmentedControl;
class VideoGrid;

class RoomPage final : public QWidget {
  Q_OBJECT

 public:
  explicit RoomPage(QWidget* parent = nullptr);

  void SetSessionState(const SessionSnapshot& state);
  void SetVideoStreams(const QVector<VideoStreamView>& streams);
  void SetVideoFrame(const QString& mid, const QImage& frame);
  void ClearVideoFrame(const QString& mid);
  void SetLocalFrame(const QImage& frame);
  void SetPreviewMode(bool preview);
  void SetBusy(bool busy);
  void OpenPolicyDialog();
  const SessionSnapshot& state() const { return state_; }

 signals:
  void micToggleRequested();
  void micFloorPressed();
  void micFloorReleased();
  void cameraToggleRequested();
  void shareToggleRequested();
  void deafenToggleRequested();
  void settingsRequested();
  void leaveRequested();
  void panelTabRequested(PanelTab tab);
  void memberSelected(const QString& uid);
  void grantFloorRequested(const QString& uid, bool allow);
  void kickMemberRequested(const QString& uid);
  void muteAllRequested();
  void policyUpdateRequested(const RoomPolicy& policy);

 protected:
  void resizeEvent(QResizeEvent* event) override;

 private:
  QWidget* BuildHeader();
  QWidget* BuildStage();
  QWidget* BuildControlBar();
  QWidget* BuildSidebar();
  void UpdateHeader();
  void UpdateControls();
  void RebuildSidePanel();
  void RefreshSidePanel();
  void UpdateMemberPanel();
  PeerSnapshot SelfPeer() const;

  SessionSnapshot state_;
  bool preview_mode_ = false;
  bool busy_ = false;

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
  QWidget* member_panel_ = nullptr;
  QLabel* member_summary_ = nullptr;
  QLabel* member_floor_ = nullptr;
  QPushButton* mute_all_ = nullptr;
  QVBoxLayout* member_rows_layout_ = nullptr;
  QHash<QString, MemberRow*> member_rows_;
  QStringList member_order_;
  QTimer* floor_timer_ = nullptr;
  RoomPolicyDialog* policy_dialog_ = nullptr;
  QVector<VideoStreamView> streams_;
  qint64 joined_at_ms_ = 0;
};

}  // namespace talk::desktop
