#pragma once

#include "domain/app_types.h"
#include "state/video_stream_registry.h"
#include "video/video_tile.h"

#include <QHash>
#include <QImage>
#include <QVector>
#include <QWidget>

namespace talk::desktop {

enum class VideoLayoutMode {
  Grid,
  Focus,
};

enum class VideoFilterMode {
  All,
  VideoOnly,
};

class VideoGrid final : public QWidget {
  Q_OBJECT

 public:
  explicit VideoGrid(
      VideoSurfaceFactory surface_factory = &CreateImageVideoSurface,
      QWidget* parent = nullptr);

  void SetState(const SessionSnapshot& state,
                const QVector<VideoStreamView>& streams);
  void SetStreams(const QVector<VideoStreamView>& streams);
  void SetLocalFrame(const QImage& frame);
  void SetLayoutMode(VideoLayoutMode mode);
  void SetFilterMode(VideoFilterMode mode);
  void FrameReady(const QString& mid, const QImage& frame);
  void FrameCleared(const QString& mid);

 signals:
  void memberSelected(const QString& uid);
  void memberFocusRequested(const QString& uid);

 protected:
  void resizeEvent(QResizeEvent* event) override;
  void paintEvent(QPaintEvent* event) override;

 private:
  struct MemberTile {
    VideoTileModel model;
    bool screen_available = false;
  };

  void SyncTiles();
  void Recalculate();
  QVector<MemberTile> BuildMembers() const;
  QVector<MemberTile> VisibleMembers(const QVector<MemberTile>& members) const;
  MemberTile BuildShare(const QVector<MemberTile>& members) const;
  void ApplyFrame(VideoTile* tile, const QString& mid);
  void PlaceTile(VideoTile* tile, const QRect& rect, bool compact,
                 bool show);
  VideoTile* TileFor(const QString& uid) const;

  VideoSurfaceFactory surface_factory_;
  SessionSnapshot state_;
  QVector<VideoStreamView> streams_;
  QHash<QString, QImage> frames_;
  QHash<QString, VideoTile*> tiles_;
  QVector<QString> tile_order_;
  VideoTile* share_tile_ = nullptr;
  QString share_uid_;
  QString share_mid_;
  VideoLayoutMode layout_ = VideoLayoutMode::Grid;
  VideoFilterMode filter_ = VideoFilterMode::All;
};

}  // namespace talk::desktop
