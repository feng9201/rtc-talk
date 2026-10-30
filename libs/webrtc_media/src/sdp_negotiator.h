#pragma once
/**
 * @brief:offer/answer 创建、设置和超时
*/
#include "rtcwin/webrtc_media/media_session.h"

#include "api/peer_connection_interface.h"
#include "rtc_base/thread.h"

#include <string>

namespace rtcwin::webrtc_media {

bool CreateAndSetLocalDescription(
    webrtc::Thread* signaling,
    webrtc::scoped_refptr<webrtc::PeerConnectionInterface> pc, bool offer,
    MediaError* error);

bool SetRemoteDescription(
    webrtc::Thread* signaling,
    webrtc::scoped_refptr<webrtc::PeerConnectionInterface> pc,
    webrtc::SdpType type, const std::string& sdp, MediaError* error);

}  // namespace rtcwin::webrtc_media
