// File: core/media/webrtc/media_stats.cpp
// Brief: 实现媒体统计聚合。
#include "media/webrtc/media_stats.h"

#include <string>

#include "media/webrtc/audio_device.h"
#include "media/webrtc/peer_connection.h"

namespace talk {

Json MediaStatsCollector::Collect(const std::vector<PeerConnection*>& peers,
                                  AudioDevice* audio) {
  Json out = Json::object();
  std::string p2p_path;
  for (auto* peer : peers) {
    if (peer == nullptr) {
      continue;
    }
    Json stats = peer->Stats();
    if (!stats.is_object() || stats.empty()) {
      continue;
    }
    out[peer->name()] = std::move(stats);
    if (peer->name() == "p2p") {
      p2p_path = peer->Path();
    }
  }
  if (!p2p_path.empty()) {
    out["p2p_path"] = p2p_path;
  }
  if (audio != nullptr) {
    Json device = audio->Stats();
    for (auto it = device.begin(); it != device.end(); ++it) {
      out[it.key()] = it.value();
    }
  }
  return out;
}

}  // namespace talk
