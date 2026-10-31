#include "peer_stats_collector.h"

#include "api/stats/rtc_stats_collector_callback.h"
#include "api/stats/rtc_stats_report.h"
#include "api/stats/rtcstats_objects.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <map>
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
      out->packets_lost += stats->packets_lost.value_or(0);
      if (stats->jitter.has_value()) {
        out->jitter_ms = std::max(out->jitter_ms, *stats->jitter * 1000.0);
      }
      const double received =
          static_cast<double>(stats->packets_received.value_or(0));
      const double lost = static_cast<double>(stats->packets_lost.value_or(0));
      if (received + lost > 0) {
        out->fraction_lost =
            std::max(out->fraction_lost, lost / (received + lost));
      }
      if (stats->audio_level.has_value()) {
        out->audio_level = std::max(out->audio_level, *stats->audio_level);
      }
    } else if (kind == "video") {
      out->video_packets_received += stats->packets_received.value_or(0);
      out->video_bytes_received += stats->bytes_received.value_or(0);
      out->packets_lost += stats->packets_lost.value_or(0);
      if (stats->jitter.has_value()) {
        out->jitter_ms = std::max(out->jitter_ms, *stats->jitter * 1000.0);
      }
      const double received =
          static_cast<double>(stats->packets_received.value_or(0));
      const double lost = static_cast<double>(stats->packets_lost.value_or(0));
      if (received + lost > 0) {
        out->fraction_lost =
            std::max(out->fraction_lost, lost / (received + lost));
      }
    }
  }
  for (const auto* stats :
       state->report->GetStatsOfType<webrtc::RTCRemoteInboundRtpStreamStats>()) {
    if (stats->round_trip_time.has_value()) {
      out->rtt_ms =
          std::max(out->rtt_ms, *stats->round_trip_time * 1000.0);
    }
    if (stats->fraction_lost.has_value()) {
      out->fraction_lost =
          std::max(out->fraction_lost, *stats->fraction_lost);
    }
  }

  std::map<std::string, std::string> local_candidates;
  std::map<std::string, std::string> remote_candidates;
  for (const auto* stats :
       state->report->GetStatsOfType<webrtc::RTCLocalIceCandidateStats>()) {
    const std::string address = stats->address.value_or(stats->ip.value_or(""));
    if (!address.empty()) local_candidates[stats->id()] = address;
  }
  for (const auto* stats :
       state->report->GetStatsOfType<webrtc::RTCRemoteIceCandidateStats>()) {
    const std::string address = stats->address.value_or(stats->ip.value_or(""));
    if (!address.empty()) remote_candidates[stats->id()] = address;
  }
  const webrtc::RTCIceCandidatePairStats* best_pair = nullptr;
  std::uint64_t best_bytes = 0;
  for (const auto* stats :
       state->report->GetStatsOfType<webrtc::RTCIceCandidatePairStats>()) {
    const bool active = stats->nominated.value_or(false) ||
                        stats->state.value_or("") == "succeeded";
    if (!active) continue;
    const std::uint64_t bytes = stats->bytes_sent.value_or(0) +
                                stats->bytes_received.value_or(0);
    if (best_pair == nullptr || bytes > best_bytes) {
      best_pair = stats;
      best_bytes = bytes;
    }
  }
  if (best_pair != nullptr) {
    if (best_pair->current_round_trip_time.has_value()) {
      out->rtt_ms =
          std::max(out->rtt_ms,
                   *best_pair->current_round_trip_time * 1000.0);
    }
    out->available_outgoing_bitrate_bps =
        best_pair->available_outgoing_bitrate.value_or(-1.0);
    const std::string local =
        local_candidates.count(best_pair->local_candidate_id.value_or(""))
            ? local_candidates[best_pair->local_candidate_id.value_or("")]
            : "";
    const std::string remote =
        remote_candidates.count(best_pair->remote_candidate_id.value_or(""))
            ? remote_candidates[best_pair->remote_candidate_id.value_or("")]
            : "";
    if (!local.empty() || !remote.empty()) {
      out->candidate_pair = local + " -> " + remote;
    }
  }
  return true;
}

}  // namespace rtcwin::webrtc_media
