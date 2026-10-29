#pragma once

#include "rtcwin/webrtc_media/media_session.h"

#include "api/video/video_frame.h"
#include "api/video/video_sink_interface.h"

#include <functional>
#include <string>

namespace rtcwin::webrtc_media {

class RemoteVideoSink final
    : public webrtc::VideoSinkInterface<webrtc::VideoFrame> {
 public:
  RemoteVideoSink(
      std::string mid, std::function<RemoteTrackInfo()> track_info,
      MediaSession::RemoteVideoCallback callback);
  ~RemoteVideoSink() override = default;

  void OnFrame(const webrtc::VideoFrame& frame) override;

 private:
  const std::string mid_;
  const std::function<RemoteTrackInfo()> track_info_;
  const MediaSession::RemoteVideoCallback callback_;
};

}  // namespace rtcwin::webrtc_media
