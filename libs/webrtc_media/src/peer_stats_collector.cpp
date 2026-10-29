#include "peer_stats_collector.h"

#include "api/stats/rtc_stats_collector_callback.h"
#include "api/stats/rtc_stats_report.h"
#include "api/stats/rtcstats_objects.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <utility>

namespace rtcwin::webrtc_media {
namespace {

struct StatsState {
  std::mutex mutex;
  std::condition_variable cv;
  bool done = false;
  webrtc::scoped_refptr<const webrtc::RTCStatsReport> report;
};

class StatsObserver : public webrtc::RTCStatsCollectorCallback {
 public:
  explicit StatsObserver(std::shared_ptr<StatsState> state)
      : state_(std::move(state)) {}

  void OnStatsDelivered(
      const webrtc::scoped_refptr<const webrtc::RTCStatsReport>& report)
      override {
    std::lock_guard<std::mutex> lock(state_->mutex);
    state_->report = report;
    state_->done = true;
    state_->cv.notify_all();
  }

 private:
  std::shared_ptr<StatsState> state_;
};

}  // namespace

bool CollectPeerMediaStats(
    webrtc::Thread* signaling,
    webrtc::scoped_refptr<webrtc::PeerConnectionInterface> pc,
    PeerMediaStats* out) {
  if (out == nullptr || signaling == nullptr || !pc) return false;
  *out = {};
  auto state = std::make_shared<StatsState>();
  auto observer = webrtc::make_ref_counted<StatsObserver>(state);
  const bool requested = signaling->BlockingCall([pc, &observer] {
    pc->GetStats(observer.get());
    return true;
  });
  if (!requested) return false;

  std::unique_lock<std::mutex> lock(state->mutex);
  if (!state->cv.wait_for(lock, std::chrono::seconds(1),
                          [&] { return state->done; }) ||
      !state->report) {
    return false;
  }
  for (const auto* stats :
       state->report->GetStatsOfType<webrtc::RTCOutboundRtpStreamStats>()) {
    const std::string kind = stats->kind.value_or("");
    if (kind == "audio") {
      out->audio_packets_sent += stats->packets_sent.value_or(0);
      out->audio_bytes_sent += stats->bytes_sent.value_or(0);
    } else if (kind == "video") {
      out->video_packets_sent += stats->packets_sent.value_or(0);
      out->video_bytes_sent += stats->bytes_sent.value_or(0);
    }
  }
  for (const auto* stats :
       state->report->GetStatsOfType<webrtc::RTCInboundRtpStreamStats>()) {
    const std::string kind = stats->kind.value_or("");
    if (kind == "audio") {
      out->audio_packets_received += stats->packets_received.value_or(0);
      out->audio_bytes_received += stats->bytes_received.value_or(0);
      if (stats->audio_level.has_value()) {
        out->audio_level = std::max(out->audio_level, *stats->audio_level);
      }
    } else if (kind == "video") {
      out->video_packets_received += stats->packets_received.value_or(0);
      out->video_bytes_received += stats->bytes_received.value_or(0);
    }
  }
  return true;
}

}  // namespace rtcwin::webrtc_media
