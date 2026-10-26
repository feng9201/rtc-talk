#pragma once

#include "domain/app_types.h"

#include <QWidget>

class QLabel;
class QScrollArea;
class QVBoxLayout;

namespace talk::desktop {

class ControlButton;
class IconButton;
class Pill;
class SegmentedControl;

class RoomPage final : public QWidget {
  Q_OBJECT

 public:
  explicit RoomPage(QWidget* parent = nullptr);

  void SetSessionState(const SessionSnapshot& state);
  void SetPreviewMode(bool preview);
  const SessionSnapshot& state() const { return state_; }

 signals:
  void micToggleRequested();
  void cameraToggleRequested();
  void shareToggleRequested();
  void deafenToggleRequested();
  void settingsRequested();
  void leaveRequested();
  void panelTabRequested(PanelTab tab);

 protected:
  void resizeEvent(QResizeEvent* event) override;

 private:
  QWidget* BuildHeader();
  QWidget* BuildStage();
  QWidget* BuildControlBar();
  QWidget* BuildSidebar();
  QWidget* BuildMemberPlaceholder();
  void UpdateHeader();
  void UpdateControls();
  void RebuildSidePanel();

  SessionSnapshot state_;
  bool preview_mode_ = false;

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
  QLabel* stage_empty_ = nullptr;
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
};

}  // namespace talk::desktop
