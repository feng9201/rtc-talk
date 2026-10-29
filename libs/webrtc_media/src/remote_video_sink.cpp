#include "remote_video_sink.h"

#include "api/video/i420_buffer.h"
#include "api/video/video_frame_buffer.h"
#include "third_party/libyuv/include/libyuv/convert.h"

#include <cstring>
#include <utility>

namespace rtcwin::webrtc_media {

RemoteVideoSink::RemoteVideoSink(
    std::string mid, std::function<RemoteTrackInfo()> track_info,
    MediaSession::RemoteVideoCallback callback)
    : mid_(std::move(mid)),
      track_info_(std::move(track_info)),
      callback_(std::move(callback)) {}

void RemoteVideoSink::OnFrame(const webrtc::VideoFrame& frame) {
  if (!callback_) return;
  webrtc::scoped_refptr<webrtc::VideoFrameBuffer> buffer =
      frame.video_frame_buffer();
  if (!buffer) return;
  webrtc::scoped_refptr<webrtc::I420BufferInterface> i420 = buffer->ToI420();
  if (!i420 || i420->width() <= 0 || i420->height() <= 0) return;

  RemoteTrackInfo info = track_info_ ? track_info_() : RemoteTrackInfo{};
  RemoteVideoFrame out;
  out.mid = mid_;
  out.uid = std::move(info.uid);
  out.source = info.source.empty() ? "camera" : std::move(info.source);
  out.width = i420->width();
  out.height = i420->height();
  out.stride_y = out.width;
  out.stride_u = (out.width + 1) / 2;
  out.stride_v = (out.width + 1) / 2;
  const int chroma_height = (out.height + 1) / 2;
  out.y.resize(static_cast<std::size_t>(out.stride_y) * out.height);
  out.u.resize(static_cast<std::size_t>(out.stride_u) * chroma_height);
  out.v.resize(static_cast<std::size_t>(out.stride_v) * chroma_height);
  libyuv::I420Copy(
      i420->DataY(), i420->StrideY(), i420->DataU(), i420->StrideU(),
      i420->DataV(), i420->StrideV(), out.y.data(), out.stride_y,
      out.u.data(), out.stride_u, out.v.data(), out.stride_v, out.width,
      out.height);
  out.timestamp_us = frame.timestamp_us();
  callback_(std::move(out));
}

}  // namespace rtcwin::webrtc_media
