// File: core/media/webrtc/video/video_frame_sink.h
// Brief: 远端视频轨的渲染 Sink（I420 -> BGRA）与跨线程回调句柄。
#ifndef TALK_MEDIA_WEBRTC_VIDEO_VIDEO_FRAME_SINK_H_
#define TALK_MEDIA_WEBRTC_VIDEO_VIDEO_FRAME_SINK_H_

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "api/video/video_frame.h"
#include "api/video/video_sink_interface.h"
#include "media/media.h"

namespace talk {

// 任意线程可替换回调；帧线程持锁拷贝回调再调用，避免死锁。
class VideoSinkHandle {
 public:
  void Set(VideoFrameSink callback);
  void Clear();
  void Notify(const VideoFrameView& view) const;

 private:
  mutable std::mutex mutex_;
  VideoFrameSink callback_;
};

// 每个远端视频轨一个 sink，负责把 I420 转成界面可直接绘制的 BGRA。
class RemoteVideoSink : public webrtc::VideoSinkInterface<webrtc::VideoFrame> {
 public:
  RemoteVideoSink(std::string mid, std::shared_ptr<VideoSinkHandle> handle);
  ~RemoteVideoSink() override = default;

  RemoteVideoSink(const RemoteVideoSink&) = delete;
  RemoteVideoSink& operator=(const RemoteVideoSink&) = delete;

  void OnFrame(const webrtc::VideoFrame& frame) override;
  const std::string& mid() const { return mid_; }

 private:
  const std::string mid_;
  const std::shared_ptr<VideoSinkHandle> handle_;
  std::mutex mutex_;
  std::vector<uint8_t> bgra_;
};

}  // namespace talk

#endif  // TALK_MEDIA_WEBRTC_VIDEO_VIDEO_FRAME_SINK_H_
