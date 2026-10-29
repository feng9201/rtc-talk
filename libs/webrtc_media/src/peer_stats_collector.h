#pragma once

#include "api/peer_connection_interface.h"
#include "rtc_base/thread.h"

#include <cstdint>

namespace rtcwin::webrtc_media {

struct PeerMediaStats {
  std::uint64_t audio_packets_sent = 0;
  std::uint64_t audio_bytes_sent = 0;
  std::uint64_t audio_packets_received = 0;
  std::uint64_t audio_bytes_received = 0;
  std::uint64_t video_packets_sent = 0;
  std::uint64_t video_bytes_sent = 0;
  std::uint64_t video_packets_received = 0;
  std::uint64_t video_bytes_received = 0;
  double audio_level = -1.0;
};

bool CollectPeerMediaStats(
    webrtc::Thread* signaling,
    webrtc::scoped_refptr<webrtc::PeerConnectionInterface> pc,
    PeerMediaStats* out);

}  // namespace rtcwin::webrtc_media
