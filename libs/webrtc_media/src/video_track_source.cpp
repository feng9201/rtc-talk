#include "video_track_source.h"

#include "api/video/i420_buffer.h"
#include "api/video/video_frame.h"
#include "rtc_base/time_utils.h"
#include "third_party/libyuv/include/libyuv/convert.h"

#include <algorithm>

namespace rtcwin::webrtc_media {

bool CameraVideoTrackSource::PushFrame(const VideoFrameData& frame) {
  if (frame.width <= 0 || frame.height <= 0 || frame.data_y == nullptr ||
      frame.data_u == nullptr || frame.data_v == nullptr ||
      frame.stride_y < frame.width ||
      frame.stride_u < (frame.width + 1) / 2 ||
      frame.stride_v < (frame.width + 1) / 2 ||
      (frame.width & 1) != 0 || (frame.height & 1) != 0) {
    return false;
  }

  webrtc::scoped_refptr<webrtc::I420Buffer> buffer =
      webrtc::I420Buffer::Create(frame.width, frame.height);
  if (!buffer) return false;
  libyuv::I420Copy(frame.data_y, frame.stride_y, frame.data_u, frame.stride_u,
                   frame.data_v, frame.stride_v, buffer->MutableDataY(),
                   buffer->StrideY(), buffer->MutableDataU(),
                   buffer->StrideU(), buffer->MutableDataV(),
                   buffer->StrideV(), frame.width, frame.height);

  // Media Foundation timestamps are device-specific and can restart or repeat
  // when a camera is reopened. WebRTC requires a monotonic capture clock, so
  // normalize every frame at the media boundary.
  const std::int64_t timestamp_us = webrtc::TimeMicros();
  webrtc::VideoFrame video =
      webrtc::VideoFrame::Builder()
          .set_video_frame_buffer(buffer)
          .set_timestamp_us(timestamp_us)
          .build();
  frames_.fetch_add(1, std::memory_order_relaxed);
  OnFrame(video);
  return true;
}

}  // namespace rtcwin::webrtc_media
