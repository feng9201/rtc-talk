#pragma once

#include <QImage>
#include <QWidget>

#include <functional>

namespace talk::desktop {

class VideoSurface : public QWidget {
  Q_OBJECT

 public:
  explicit VideoSurface(QWidget* parent = nullptr);
  ~VideoSurface() override = default;

  virtual bool hasFrame() const = 0;
  virtual QImage frame() const = 0;

 public slots:
  virtual void SetFrame(const QImage& frame) = 0;
  virtual void ClearFrame() = 0;

 signals:
  void frameChanged();
};

class ImageVideoSurface final : public VideoSurface {
  Q_OBJECT

 public:
  explicit ImageVideoSurface(QWidget* parent = nullptr);

  bool hasFrame() const override { return !frame_.isNull(); }
  QImage frame() const override { return frame_; }
  void SetFrame(const QImage& frame) override;
  void ClearFrame() override;
  void SetMirrored(bool mirrored);
  void SetPlaceholder(const QString& text);

 protected:
  void paintEvent(QPaintEvent* event) override;

 private:
  QImage frame_;
  QString placeholder_;
  bool mirrored_ = false;
};

using VideoSurfaceFactory =
    std::function<VideoSurface*(QWidget* parent)>;

VideoSurface* CreateImageVideoSurface(QWidget* parent);

}  // namespace talk::desktop
