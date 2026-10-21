// File: core/media/webrtc/media_stats.h
// Brief: 聚合 send/recv/p2p 与设备统计。
#ifndef TALK_MEDIA_WEBRTC_MEDIA_STATS_H_
#define TALK_MEDIA_WEBRTC_MEDIA_STATS_H_

#include <vector>

#include "base/json.h"

namespace talk {

class AudioDevice;
class PeerConnection;

class MediaStatsCollector {
 public:
  static Json Collect(const std::vector<PeerConnection*>& peers,
                      AudioDevice* audio);
};

}  // namespace talk

#endif  // TALK_MEDIA_WEBRTC_MEDIA_STATS_H_
