#include "rtcwin/application/frame_health_monitor.h"

#include <algorithm>
#include <map>
#include <set>

namespace rtcwin::application {
namespace {

bool MemberHasSource(const domain::SessionSnapshot& snapshot,
                     const std::string& uid,
                     domain::VideoSource source) {
  if (uid.empty()) return false;
  if (uid == snapshot.self_uid) {
    return source == domain::VideoSource::Screen ? snapshot.sharing
                                                  : snapshot.camera_on;
  }
  const auto peer = std::find_if(
      snapshot.peers.begin(), snapshot.peers.end(),
      [&uid](const domain::PeerState& member) { return member.uid == uid; });
  if (peer == snapshot.peers.end()) return false;
  return source == domain::VideoSource::Screen ? peer->screen_on
                                                : peer->camera_on;
}

FrameHealthState Evaluate(const std::int64_t last_frame_ms,
                          const bool producer_paused,
                          const std::int64_t now_ms) {
  if (producer_paused) return FrameHealthState::Paused;
  if (last_frame_ms <= 0) return FrameHealthState::Waiting;
  if (now_ms - last_frame_ms >= FrameHealthMonitor::kStaleAfterMs) {
    return FrameHealthState::Stale;
  }
  return FrameHealthState::Healthy;
}

}  // namespace

void FrameHealthMonitor::Sync(const domain::SessionSnapshot& snapshot) {
  std::set<std::string> observed;
  std::map<std::string, bool> stream_paused;

  for (const auto& stream : snapshot.streams) {
    if (stream.uid.empty()) continue;
    const domain::VideoStreamKey key{stream.uid, stream.source};
    const std::string name = domain::VideoStreamKeyName(key);
    observed.insert(name);
    stream_paused[name] = stream.producer_paused;

    Entry& entry = streams_[name];
    entry.key = key;
    if (!stream.mid.empty()) entry.mid = stream.mid;
    entry.producer_paused =
        stream.producer_paused ||
        !MemberHasSource(snapshot, key.uid, key.source);
  }

  const auto observe_member =
      [this, &observed, &stream_paused](const std::string& uid,
                                        bool camera_on, bool screen_on) {
        if (camera_on) {
          const domain::VideoStreamKey key{uid,
                                           domain::VideoSource::Camera};
          const std::string name = domain::VideoStreamKeyName(key);
          observed.insert(name);
          Entry& entry = streams_[name];
          entry.key = key;
          const auto paused = stream_paused.find(name);
          entry.producer_paused =
              paused != stream_paused.end() ? paused->second : false;
        }
        if (screen_on) {
          const domain::VideoStreamKey key{uid,
                                           domain::VideoSource::Screen};
          const std::string name = domain::VideoStreamKeyName(key);
          observed.insert(name);
          Entry& entry = streams_[name];
          entry.key = key;
          const auto paused = stream_paused.find(name);
          entry.producer_paused =
              paused != stream_paused.end() ? paused->second : false;
        }
      };

  observe_member(snapshot.self_uid, snapshot.camera_on, snapshot.sharing);
  for (const auto& peer : snapshot.peers) {
    observe_member(peer.uid, peer.camera_on, peer.screen_on);
  }

  for (auto it = streams_.begin(); it != streams_.end();) {
    if (observed.find(it->first) == observed.end()) {
      it = streams_.erase(it);
    } else {
      ++it;
    }
  }
}

void FrameHealthMonitor::NoteFrame(const domain::VideoStreamKey& key,
                                   const std::string& mid,
                                   std::int64_t now_ms) {
  Entry& entry = streams_[domain::VideoStreamKeyName(key)];
  entry.key = key;
  if (!mid.empty()) entry.mid = mid;
  entry.producer_paused = false;
  entry.last_frame_ms = now_ms;
}

void FrameHealthMonitor::RemoveExcept(
    const std::vector<domain::VideoStreamKey>& keys) {
  std::set<std::string> active;
  for (const auto& key : keys) {
    active.insert(domain::VideoStreamKeyName(key));
  }
  for (auto it = streams_.begin(); it != streams_.end();) {
    if (active.find(it->first) == active.end()) {
      it = streams_.erase(it);
    } else {
      ++it;
    }
  }
}

FrameHealthState FrameHealthMonitor::StateFor(
    const domain::VideoStreamKey& key, std::int64_t now_ms) const {
  const auto it = streams_.find(domain::VideoStreamKeyName(key));
  if (it == streams_.end()) return FrameHealthState::Removed;
  return Evaluate(it->second.last_frame_ms, it->second.producer_paused,
                  now_ms);
}

std::vector<domain::VideoStreamKey> FrameHealthMonitor::StaleStreams(
    std::int64_t now_ms) const {
  std::vector<domain::VideoStreamKey> stale;
  for (const auto& item : streams_) {
    const Entry& entry = item.second;
    if (Evaluate(entry.last_frame_ms, entry.producer_paused, now_ms) ==
        FrameHealthState::Stale) {
      stale.push_back(entry.key);
    }
  }
  return stale;
}

std::vector<FrameStreamStatus> FrameHealthMonitor::Statuses(
    std::int64_t now_ms) const {
  std::vector<FrameStreamStatus> result;
  result.reserve(streams_.size());
  for (const auto& item : streams_) {
    const Entry& entry = item.second;
    result.push_back(FrameStreamStatus{
        entry.key, entry.mid, entry.producer_paused, entry.last_frame_ms,
        Evaluate(entry.last_frame_ms, entry.producer_paused, now_ms)});
  }
  return result;
}

}  // namespace rtcwin::application
