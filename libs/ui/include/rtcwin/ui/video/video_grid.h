#pragma once

#include "rtcwin/domain/types.h"
#include "rtcwin/ui/video/video_tile.h"

#include <QHash>
#include <QVector>
#include <QWidget>

class QGridLayout;

namespace rtcwin::ui {

class VideoGrid final : public QWidget {
  Q_OBJECT

 public:
  explicit VideoGrid(QWidget* parent = nullptr);

  void SetState(const domain::SessionSnapshot& snapshot);
  void SetLayoutMode(domain::StageLayout layout);
  void SetFilter(domain::StageFilter filter);

 signals:
  void MemberSelected(const QString& uid);
  void MemberFocusRequested(const QString& uid);

 protected:
  void resizeEvent(QResizeEvent* event) override;
  void paintEvent(QPaintEvent* event) override;

 private:
  QVector<domain::PeerState> VisibleMembers() const;
  void Rebuild();
  void ApplyPlacement();
  VideoTile* EnsureTile(const domain::PeerState& member);

  domain::SessionSnapshot snapshot_;
  domain::StageLayout layout_ = domain::StageLayout::Grid;
  domain::StageFilter filter_ = domain::StageFilter::All;
  QGridLayout* layout_engine_ = nullptr;
  QHash<QString, VideoTile*> tiles_;
};

}  // namespace rtcwin::ui
