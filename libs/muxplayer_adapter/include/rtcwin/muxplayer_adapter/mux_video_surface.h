#pragma once

#include "rtcwin/domain/types.h"

#include <QString>
#include <QWidget>

#include <memory>

class GLPlayWidget;

namespace muxplay {
class FramePump;
}

namespace rtcwin::muxplayer_adapter {

class MuxVideoSurface final : public QWidget {
  Q_OBJECT

 public:
  explicit MuxVideoSurface(QWidget* parent = nullptr);
  ~MuxVideoSurface() override;

  bool Open();
  bool SubmitFrame(const domain::VideoFrame& frame);
  void SetPlaceholder(const QString& text);
  void Clear();
  void Close();
  bool has_frame() const { return has_frame_; }

 protected:
  void paintEvent(QPaintEvent* event) override;
  void resizeEvent(QResizeEvent* event) override;

 private:
  void LayoutSurface();

  std::unique_ptr<muxplay::FramePump> pump_;
  GLPlayWidget* gl_widget_ = nullptr;
  bool opened_ = false;
  bool has_frame_ = false;
  QString placeholder_ = QStringLiteral("等待 muxPlayer 画面");
};

}  // namespace rtcwin::muxplayer_adapter
