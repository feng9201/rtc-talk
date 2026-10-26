#pragma once

#include "video/video_surface.h"

#include <QWidget>

class QLabel;

namespace talk::desktop {

struct VideoTileModel {
  QString uid;
  QString displayName;
  QString role;
  QString cameraMid;
  QString screenMid;
  bool self = false;
  bool cameraOn = false;
  bool screenOn = false;
  bool micMuted = true;
  bool speaking = false;
  bool receiving = false;
  bool screenReceiving = false;
};

class VideoTile final : public QWidget {
  Q_OBJECT

 public:
  explicit VideoTile(
      VideoSurfaceFactory surface_factory = &CreateImageVideoSurface,
      QWidget* parent = nullptr);

  void SetModel(const VideoTileModel& model);
  void SetFrame(const QImage& frame);
  void ClearFrame();
  void SetSelected(bool selected);
  void SetCompact(bool compact);
  void SetPresentation(bool presentation);
  QString uid() const { return model_.uid; }
  QString cameraMid() const { return model_.cameraMid; }
  QString screenMid() const { return model_.screenMid; }

 signals:
  void selected(const QString& uid);
  void focusRequested(const QString& uid);

 protected:
  void paintEvent(QPaintEvent* event) override;
  void resizeEvent(QResizeEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseDoubleClickEvent(QMouseEvent* event) override;
  void enterEvent(QEvent* event) override;
  void leaveEvent(QEvent* event) override;

 private:
  void UpdateOverlays();
  void PositionOverlays();
  QString Title() const;

  VideoTileModel model_;
  VideoSurface* surface_ = nullptr;
  QLabel* name_ = nullptr;
  QLabel* badge_ = nullptr;
  QLabel* mic_ = nullptr;
  QLabel* camera_ = nullptr;
  bool selected_ = false;
  bool compact_ = false;
  bool presentation_ = false;
  bool hovered_ = false;
  bool has_frame_ = false;
};

}  // namespace talk::desktop
