#pragma once

#include <QImage>
#include <QWidget>

namespace rtcwin::ui {

class VideoSurface : public QWidget {
  Q_OBJECT

 public:
  explicit VideoSurface(QWidget* parent = nullptr);
  ~VideoSurface() override = default;

  virtual bool HasFrame() const = 0;
  virtual void SetFrame(const QImage& frame) = 0;
  virtual void Clear() = 0;
  virtual void SetPlaceholder(const QString& text) = 0;
};

class ImageVideoSurface final : public VideoSurface {
  Q_OBJECT

 public:
  explicit ImageVideoSurface(QWidget* parent = nullptr);

  bool HasFrame() const override { return !frame_.isNull(); }
  void SetFrame(const QImage& frame) override;
  void Clear() override;
  void SetPlaceholder(const QString& text) override;

 protected:
  void paintEvent(QPaintEvent* event) override;

 private:
  QImage frame_;
  QString placeholder_ = QStringLiteral("等待 muxPlayer 画面");
};

}  // namespace rtcwin::ui
