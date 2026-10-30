#pragma once

#include "rtcwin/application/video_scene.h"
#include "rtcwin/domain/types.h"
#include "rtcwin/ui/video/video_layout.h"
#include "rtcwin/ui/video/video_tile.h"

#include <QHash>
#include <QWidget>

#include <memory>

class QGridLayout;

namespace rtcwin::ui {

class VideoGrid final : public QWidget {
  Q_OBJECT

 public:
  explicit VideoGrid(QWidget* parent = nullptr);

  void SetState(const domain::SessionSnapshot& snapshot);
  void SetFrame(const domain::VideoFrame& frame);
  void SetLayoutMode(domain::StageLayout layout);
  void SetFilter(domain::StageFilter filter);

 signals:
  void MemberSelected(const QString& uid);
  void MemberFocusRequested(const QString& uid);

 protected:
  void resizeEvent(QResizeEvent* event) override;
  void paintEvent(QPaintEvent* event) override;

 private:
  domain::PeerState MemberForUid(const std::string& uid) const;
  void Rebuild();
  void ApplyPlacement();
  VideoTile* EnsureTile(const application::VideoSceneItem& item);
  void ApplyFrame(VideoTile* tile,
                  const application::VideoSceneItem& item);
  void ApplyFrameToMatchingTiles(const domain::VideoFrame& frame);
  static QString TileKey(const application::VideoSceneItem& item);

  domain::SessionSnapshot snapshot_;
  domain::StageLayout layout_ = domain::StageLayout::Grid;
  domain::StageFilter filter_ = domain::StageFilter::All;
  application::VideoScene scene_;
  VideoLayoutPlan layout_plan_;
  int placed_rows_ = 0;
  int placed_columns_ = 0;
  QGridLayout* layout_engine_ = nullptr;
  QHash<QString, VideoTile*> tiles_;
  QHash<QString, std::shared_ptr<domain::VideoFrame>> frames_;
};

}  // namespace rtcwin::ui
