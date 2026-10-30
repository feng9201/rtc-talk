#pragma once

#include "rtcwin/domain/types.h"

#include <QWidget>

class QLabel;

namespace rtcwin::muxplayer_adapter {
class MuxVideoSurface;
}

namespace rtcwin::ui {

class VideoTile final : public QWidget {
  Q_OBJECT

 public:
  explicit VideoTile(QWidget* parent = nullptr);
  ~VideoTile() override;

  void SetMember(const domain::PeerState& member, bool self, bool selected,
                 bool compact = false);
  void SetSource(domain::VideoSource source, bool has_camera_pip);
  void AttachMainSurface(
      muxplayer_adapter::MuxVideoSurface* surface);
  void AttachPipSurface(
      muxplayer_adapter::MuxVideoSurface* surface);
  void DetachSurfaces();
  void SetFrame(const domain::VideoFrame& frame);
  void SetPipFrame(const domain::VideoFrame& frame);
  void ClearFrame();
  void ClearPipFrame();
  QString uid() const { return uid_; }

 signals:
  void Selected(const QString& uid);
  void FocusRequested(const QString& uid);

 protected:
  void resizeEvent(QResizeEvent* event) override;
  void paintEvent(QPaintEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseDoubleClickEvent(QMouseEvent* event) override;

 private:
  void UpdateOverlay();
  void positionOverlays();
  void positionPipSurface();

  QString uid_;
  domain::PeerState member_;
  bool self_ = false;
  bool selected_ = false;
  bool compact_ = false;
  domain::VideoSource source_ = domain::VideoSource::Camera;
  bool has_camera_pip_ = false;
  muxplayer_adapter::MuxVideoSurface* surface_ = nullptr;
  muxplayer_adapter::MuxVideoSurface* pip_surface_ = nullptr;
  QLabel* pip_border_ = nullptr;
  QLabel* pip_label_ = nullptr;
  QLabel* name_ = nullptr;
  QLabel* status_ = nullptr;
  QLabel* role_ = nullptr;
};

}  // namespace rtcwin::ui
