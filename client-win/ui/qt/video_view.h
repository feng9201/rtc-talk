// File: ui/qt/video_view.h
// Brief: 远端视频视图：等比居中绘制，按帧间隔合并刷新。
#ifndef TALK_UI_VIDEO_VIEW_H_
#define TALK_UI_VIDEO_VIEW_H_

#include <QImage>
#include <QWidget>

class QTimer;

class VideoView : public QWidget {
  Q_OBJECT

 public:
  explicit VideoView(QWidget* parent = nullptr);

  // 只在 UI 线程调用：保存最新一帧，由定时器合并重绘。
  void PushFrame(const QImage& frame);

  // 清空画面（离开房间或断线）。
  void Clear();

 protected:
  void paintEvent(QPaintEvent* event) override;

 private:
  // 把待显示帧换成当前帧并触发一次重绘。
  void ApplyPending();

  QImage frame_;
  QImage pending_;
  QTimer* timer_ = nullptr;
};

#endif  // TALK_UI_VIDEO_VIEW_H_
