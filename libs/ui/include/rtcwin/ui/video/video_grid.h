#pragma once

#include "rtcwin/application/frame_health_monitor.h"
#include "rtcwin/application/video_scene.h"
#include "rtcwin/domain/types.h"
#include "rtcwin/ui/video/video_layout.h"
#include "rtcwin/ui/video/video_surface_pool.h"
#include "rtcwin/ui/video/video_tile.h"

#include <QHash>
#include <QSet>
#include <QWidget>

#include <map>
#include <memory>

class QGridLayout;
class QTimer;

namespace rtcwin::ui {

struct VideoGridDiagnostics {
  int tiles = 0;
  int active_surfaces = 0;
  int idle_surfaces = 0;
  int cached_frames = 0;
  int stale_streams = 0;
};

class VideoGrid final : public QWidget {
  Q_OBJECT

 public:
  explicit VideoGrid(QWidget* parent = nullptr);
  ~VideoGrid() override;

  void SetState(const domain::SessionSnapshot& snapshot);
  void SetFrame(const domain::VideoFrame& frame);
  void SetLayoutMode(domain::StageLayout layout);
  void SetFilter(domain::StageFilter filter);
  VideoGridDiagnostics Diagnostics() const;

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
  void BindSurfaces();
  void ClearSurfaceLeases();
  void TickFrameHealth();
  void ClearStreamFrames(const domain::VideoStreamKey& key);
  bool IsSurfaceRenderable(const domain::VideoStreamKey& key) const;
  static domain::VideoStreamKey StreamKey(
      const application::VideoSceneItem& item);
  static QString TileKey(const application::VideoSceneItem& item);

  domain::SessionSnapshot snapshot_;
  domain::StageLayout layout_ = domain::StageLayout::Grid;
  domain::StageFilter filter_ = domain::StageFilter::All;
  application::VideoScene scene_;
  VideoLayoutPlan layout_plan_;
  int placed_rows_ = 0;
  int placed_columns_ = 0;
  QGridLayout* layout_engine_ = nullptr;
  QTimer* health_timer_ = nullptr;
  std::unique_ptr<VideoSurfacePool> surface_pool_;
  std::map<QString, std::unique_ptr<VideoSurfaceLease>> surface_leases_;
  application::FrameHealthMonitor health_monitor_;
  QHash<QString, VideoTile*> tiles_;
  QSet<QString> cleared_stale_streams_;
  QHash<QString, std::shared_ptr<domain::VideoFrame>> frames_;
};

}  // namespace rtcwin::ui
