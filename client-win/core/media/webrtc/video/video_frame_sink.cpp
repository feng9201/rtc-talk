// File: core/media/webrtc/video/video_frame_sink.cpp
// Brief: 实现远端帧的 I420 -> BGRA 转换与回调分发。
#include "media/webrtc/video/video_frame_sink.h"

#include <utility>

#include "api/video/i420_buffer.h"
#include "api/video/video_frame_buffer.h"
#include "third_party/libyuv/include/libyuv/convert_argb.h"

namespace talk {

void VideoSinkHandle::Set(VideoFrameSink callback) {
  std::lock_guard<std::mutex> lock(mutex_);
  callback_ = std::move(callback);
}

void VideoSinkHandle::Clear() {
  std::lock_guard<std::mutex> lock(mutex_);
  callback_ = nullptr;
}

void VideoSinkHandle::Notify(const VideoFrameView& view) const {
  VideoFrameSink callback;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    callback = callback_;
  }
  if (callback) {
    callback(view);
  }
}

RemoteVideoSink::RemoteVideoSink(std::string mid,
                                 std::shared_ptr<VideoSinkHandle> handle)
    : mid_(std::move(mid)), handle_(std::move(handle)) {}

void RemoteVideoSink::OnFrame(const webrtc::VideoFrame& frame) {
  if (!handle_) {
    return;
  }
  webrtc::scoped_refptr<webrtc::VideoFrameBuffer> buffer =
      frame.video_frame_buffer();
  if (!buffer) {
    return;
  }
  // 解码输出可能是 NV12 等格式，统一转 I420 再转 BGRA。
  webrtc::scoped_refptr<webrtc::I420BufferInterface> i420 = buffer->ToI420();
  if (!i420) {
    return;
  }
  const int width = i420->width();
  const int height = i420->height();
  if (width <= 0 || height <= 0) {
    return;
  }
  const int stride = width * 4;

  std::lock_guard<std::mutex> lock(mutex_);
  const size_t needed = static_cast<size_t>(stride) * height;
  if (bgra_.size() < needed) {
    bgra_.resize(needed);
  }
  libyuv::I420ToARGB(i420->DataY(), i420->StrideY(), i420->DataU(),
                     i420->StrideU(), i420->DataV(), i420->StrideV(),
                     bgra_.data(), stride, width, height);

  VideoFrameView view;
  view.mid = mid_.c_str();
  view.bgra = bgra_.data();
  view.width = width;
  view.height = height;
  view.stride = stride;
  view.timestamp_us = frame.timestamp_us();
  handle_->Notify(view);
}

}  // namespace talk
