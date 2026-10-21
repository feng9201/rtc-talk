// File: core/media/webrtc/peer_connection.cpp
// Brief: 实现 PeerConnection 包装、SDP 操作与统计。
#include "media/webrtc/peer_connection.h"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <future>
#include <utility>

#include "api/candidate.h"
#include "api/jsep.h"
#include "api/make_ref_counted.h"
#include "api/media_stream_interface.h"
#include "api/rtp_transceiver_interface.h"
#include "api/stats/rtc_stats_collector_callback.h"
#include "api/stats/rtc_stats_report.h"
#include "api/stats/rtcstats_objects.h"
#include "media/webrtc/wait_utils.h"
#include "p2p/base/port.h"

namespace talk {
namespace {

constexpr int kAudioMaxBitrateBps = 32000;

class CreateObserver : public webrtc::CreateSessionDescriptionObserver {
 public:
  std::promise<std::unique_ptr<webrtc::SessionDescriptionInterface>> promise;

  void OnSuccess(webrtc::SessionDescriptionInterface* description) override {
    promise.set_value(
        std::unique_ptr<webrtc::SessionDescriptionInterface>(description));
  }

  void OnFailure(webrtc::RTCError) override { promise.set_value(nullptr); }
};

class SetObserver : public webrtc::SetSessionDescriptionObserver {
 public:
  std::promise<bool> promise;

  void OnSuccess() override { promise.set_value(true); }
  void OnFailure(webrtc::RTCError) override { promise.set_value(false); }
};

class StatsObserver : public webrtc::RTCStatsCollectorCallback {
 public:
  std::promise<webrtc::scoped_refptr<const webrtc::RTCStatsReport>> promise;

  void OnStatsDelivered(
      const webrtc::scoped_refptr<const webrtc::RTCStatsReport>& report)
      override {
    promise.set_value(report);
  }
};

}  // namespace

PeerConnection::PeerConnection(std::string name, Media::Event event,
                               webrtc::Thread* signaling)
    : name_(std::move(name)),
      event_(std::move(event)),
      signaling_(signaling) {}

PeerConnection::~PeerConnection() { Close(); }

bool PeerConnection::Create(
    webrtc::PeerConnectionFactoryInterface* factory,
    const webrtc::PeerConnectionInterface::RTCConfiguration& config) {
  if (factory == nullptr) {
    return false;
  }
  return RunOnSignaling([this, factory, &config] {
    if (pc_) {
      return true;
    }
    auto result = factory->CreatePeerConnectionOrError(
        config, webrtc::PeerConnectionDependencies(this));
    if (!result.ok()) {
      return false;
    }
    pc_ = result.MoveValue();
    return true;
  });
}

void PeerConnection::Close() {
  if (signaling_ == nullptr) {
    return;
  }
  RunOnSignaling([this] {
    if (pc_) {
      pc_->Close();
      pc_ = nullptr;
    }
    senders_.clear();
    path_.clear();
  });
}

bool PeerConnection::AddAudioTrack(
    webrtc::scoped_refptr<webrtc::AudioTrackInterface> track,
    webrtc::RtpTransceiverDirection direction,
    const std::string& stream_id) {
  return RunOnSignaling([this, track = std::move(track), direction,
                         stream_id] {
    if (!pc_) {
      return false;
    }
    if (senders_.find(TrackKind::kAudio) != senders_.end()) {
      return true;
    }
    webrtc::RtpTransceiverInit init;
    init.direction = direction;
    init.stream_ids = {stream_id};
    auto result = pc_->AddTransceiver(track, init);
    if (!result.ok()) {
      return false;
    }
    senders_[TrackKind::kAudio] = result.value()->sender();
    return true;
  });
}

bool PeerConnection::CreateAndSetLocal(bool offer) {
  if (!RunOnSignaling([this] { return pc_ != nullptr; })) {
    return false;
  }
  auto create_observer = webrtc::make_ref_counted<CreateObserver>();
  auto create_future = create_observer->promise.get_future();
  RunOnSignaling([this, offer, &create_observer] {
    webrtc::PeerConnectionInterface::RTCOfferAnswerOptions options;
    if (offer) {
      pc_->CreateOffer(create_observer.get(), options);
    } else {
      pc_->CreateAnswer(create_observer.get(), options);
    }
  });
  auto description = WaitFor(std::move(create_future));
  if (!description) {
    return false;
  }
  auto set_observer = webrtc::make_ref_counted<SetObserver>();
  auto set_future = set_observer->promise.get_future();
  auto* raw = description.release();
  RunOnSignaling([this, &set_observer, raw] {
    pc_->SetLocalDescription(set_observer.get(), raw);
  });
  return WaitFor(std::move(set_future));
}

bool PeerConnection::SetRemote(webrtc::SdpType type, const std::string& sdp) {
  if (!RunOnSignaling([this] { return pc_ != nullptr; })) {
    return false;
  }
  auto description = webrtc::CreateSessionDescription(type, sdp);
  if (!description) {
    return false;
  }
  auto observer = webrtc::make_ref_counted<SetObserver>();
  auto future = observer->promise.get_future();
  auto* raw = description.release();
  RunOnSignaling([this, &observer, raw] {
    pc_->SetRemoteDescription(observer.get(), raw);
  });
  return WaitFor(std::move(future));
}

bool PeerConnection::AddIceCandidate(const std::string& candidate,
                                     const std::string& mid, int index) {
  if (candidate.empty()) {
    return true;  // 结束标记，无需添加。
  }
  if (!RunOnSignaling([this] { return pc_ != nullptr; })) {
    return false;
  }
  auto ice = webrtc::IceCandidate::Create(mid, index, candidate);
  if (!ice) {
    return false;
  }
  return RunOnSignaling(
      [this, &ice] { return pc_->AddIceCandidate(ice.get()); });
}

bool PeerConnection::GetLocalSdp(std::string* sdp) {
  return RunOnSignaling([this, sdp] {
    if (!pc_ || pc_->local_description() == nullptr) {
      return false;
    }
    return pc_->local_description()->ToString(sdp);
  });
}

bool PeerConnection::FirstMid(std::string* mid) {
  return RunOnSignaling([this, mid] {
    if (!pc_) {
      return false;
    }
    const auto transceivers = pc_->GetTransceivers();
    if (transceivers.empty() || !transceivers.front()->mid().has_value()) {
      return false;
    }
    *mid = *transceivers.front()->mid();
    return true;
  });
}

bool PeerConnection::SetSenderActive(bool active) {
  return RunOnSignaling([this, active] {
    bool ok = true;
    for (const auto& item : senders_) {
      if (!item.second) {
        continue;
      }
      auto parameters = item.second->GetParameters();
      for (auto& encoding : parameters.encodings) {
        encoding.active = active;
        if (item.first == TrackKind::kAudio) {
          encoding.max_bitrate_bps = kAudioMaxBitrateBps;
        }
      }
      if (!item.second->SetParameters(parameters).ok()) {
        ok = false;
      }
    }
    return ok;
  });
}

void PeerConnection::SetReceivePaused(bool paused) {
  receive_paused_ = paused;
  RunOnSignaling(
      [this, paused] { ApplyReceiveStateOnSignaling(paused); });
}

void PeerConnection::set_gain(double gain) {
  gain_ = gain;
  RunOnSignaling([this, gain] { ApplyGainOnSignaling(gain); });
}

void PeerConnection::ApplyGainOnSignaling(double gain) {
  if (!pc_) {
    return;
  }
  for (const auto& receiver : pc_->GetReceivers()) {
    auto track = receiver->track();
    if (track && track->kind() == "audio") {
      auto* audio = static_cast<webrtc::AudioTrackInterface*>(track.get());
      if (audio->GetSource()) {
        audio->GetSource()->SetVolume(gain);
      }
    }
  }
}

void PeerConnection::ApplyReceiveStateOnSignaling(bool paused) {
  if (!pc_) {
    return;
  }
  for (const auto& receiver : pc_->GetReceivers()) {
    auto track = receiver->track();
    if (track) {
      track->set_enabled(!paused);
    }
  }
}

Json PeerConnection::Stats() {
  struct Request {
    webrtc::scoped_refptr<StatsObserver> observer;
    std::future<webrtc::scoped_refptr<const webrtc::RTCStatsReport>> future;
  };
  Request request;
  RunOnSignaling([this, &request] {
    if (!pc_) {
      return;
    }
    request.observer = webrtc::make_ref_counted<StatsObserver>();
    request.future = request.observer->promise.get_future();
    pc_->GetStats(request.observer.get());
  });
  if (!request.observer) {
    return Json::object();
  }
  auto report = WaitFor(std::move(request.future));
  if (!report.get()) {
    return Json::object();
  }
  uint64_t received = 0;
  uint64_t received_bytes = 0;
  uint64_t sent = 0;
  uint64_t sent_bytes = 0;
  int64_t lost = 0;
  double jitter = 0;
  double audio_level = -1.0;
  double audio_energy = 0;
  uint64_t samples = 0;
  uint64_t concealed = 0;
  for (const auto* stats :
       report->GetStatsOfType<webrtc::RTCInboundRtpStreamStats>()) {
    if (stats->kind.value_or("") == "audio") {
      received += stats->packets_received.value_or(0);
      received_bytes += stats->bytes_received.value_or(0);
      lost += stats->packets_lost.value_or(0);
      jitter = std::max(jitter, stats->jitter.value_or(0));
      if (stats->audio_level.has_value()) {
        audio_level = std::max(audio_level, *stats->audio_level);
      }
      audio_energy += stats->total_audio_energy.value_or(0);
      samples += stats->total_samples_received.value_or(0);
      concealed += stats->concealed_samples.value_or(0);
    }
  }
  for (const auto* stats :
       report->GetStatsOfType<webrtc::RTCOutboundRtpStreamStats>()) {
    if (stats->kind.value_or("") == "audio") {
      sent += stats->packets_sent.value_or(0);
      sent_bytes += stats->bytes_sent.value_or(0);
    }
  }
  std::string dtls_state;
  std::string ice_state;
  uint64_t transport_received_bytes = 0;
  uint64_t transport_received_packets = 0;
  uint64_t transport_sent_bytes = 0;
  uint64_t transport_sent_packets = 0;
  std::string selected_pair_id;
  for (const auto* stats :
       report->GetStatsOfType<webrtc::RTCTransportStats>()) {
    dtls_state = stats->dtls_state.value_or(std::string());
    ice_state = stats->ice_state.value_or(std::string());
    transport_received_bytes += stats->bytes_received.value_or(0);
    transport_received_packets += stats->packets_received.value_or(0);
    transport_sent_bytes += stats->bytes_sent.value_or(0);
    transport_sent_packets += stats->packets_sent.value_or(0);
    if (stats->selected_candidate_pair_id.has_value()) {
      selected_pair_id = *stats->selected_candidate_pair_id;
    }
  }
  std::map<std::string, std::string> candidate_text;
  const auto add_candidate = [&candidate_text](
                                 const webrtc::RTCIceCandidateStats& stats) {
    std::string text = stats.candidate_type.value_or(std::string("?"));
    const std::string protocol = stats.protocol.value_or(std::string());
    if (!protocol.empty()) {
      text += "/" + protocol;
    }
    const std::string address = stats.address.value_or(std::string());
    if (!address.empty()) {
      text += " " + address;
      if (stats.port.has_value()) {
        text += ":" + std::to_string(*stats.port);
      }
    }
    candidate_text[stats.id()] = std::move(text);
  };
  for (const auto* stats :
       report->GetStatsOfType<webrtc::RTCIceCandidateStats>()) {
    add_candidate(*stats);
  }
  // 本地/远端候选使用独立 type，不会出现在基类的 GetStatsOfType 中。
  for (const auto* stats :
       report->GetStatsOfType<webrtc::RTCLocalIceCandidateStats>()) {
    add_candidate(*stats);
  }
  for (const auto* stats :
       report->GetStatsOfType<webrtc::RTCRemoteIceCandidateStats>()) {
    add_candidate(*stats);
  }
  std::string selected_pair;
  double rtt_ms = -1.0;
  for (const auto* stats :
       report->GetStatsOfType<webrtc::RTCIceCandidatePairStats>()) {
    bool matches = false;
    if (!selected_pair_id.empty()) {
      matches = stats->id() == selected_pair_id;
    } else {
      matches = stats->state.value_or("") == "succeeded" &&
                stats->nominated.value_or(false);
    }
    if (!matches) {
      continue;
    }
    const auto local = candidate_text.find(
        stats->local_candidate_id.value_or(std::string()));
    const auto remote = candidate_text.find(
        stats->remote_candidate_id.value_or(std::string()));
    selected_pair = "本地=" +
                    (local != candidate_text.end() ? local->second : "?") +
                    " 远端=" +
                    (remote != candidate_text.end() ? remote->second : "?");
    if (stats->current_round_trip_time.has_value()) {
      rtt_ms = *stats->current_round_trip_time * 1000.0;
    }
    break;
  }
  return {{"received", received},
          {"received_bytes", received_bytes},
          {"sent", sent},
          {"sent_bytes", sent_bytes},
          {"lost", lost},
          {"jitter_seconds", jitter},
          {"audio_level", audio_level},
          {"audio_energy", audio_energy},
          {"samples_received", samples},
          {"concealed_samples", concealed},
          {"dtls_state", dtls_state},
          {"ice_state", ice_state},
          {"transport_received_bytes", transport_received_bytes},
          {"transport_received_packets", transport_received_packets},
          {"transport_sent_bytes", transport_sent_bytes},
          {"transport_sent_packets", transport_sent_packets},
          {"selected_pair", selected_pair},
          {"rtt_ms", rtt_ms}};
}

std::string PeerConnection::Path() {
  return RunOnSignaling([this] { return path_; });
}

void PeerConnection::OnSignalingChange(
    webrtc::PeerConnectionInterface::SignalingState) {}

void PeerConnection::OnDataChannel(
    webrtc::scoped_refptr<webrtc::DataChannelInterface>) {}

void PeerConnection::OnIceCandidate(const webrtc::IceCandidate* candidate) {
  if (!event_ || name_ != "p2p") {
    return;
  }
  Json data = {
      {"candidate", candidate != nullptr ? candidate->ToString()
                                         : std::string()}};
  if (candidate != nullptr) {
    data["sdp_mid"] = candidate->sdp_mid();
    data["sdp_mline_index"] = candidate->sdp_mline_index();
  }
  event_("p2p.candidate", std::move(data));
}

void PeerConnection::OnIceGatheringChange(
    webrtc::PeerConnectionInterface::IceGatheringState) {}

void PeerConnection::OnIceSelectedCandidatePairChanged(
    const webrtc::CandidatePairChangeEvent& event) {
  using webrtc::IceCandidateType;
  const auto& pair = event.selected_candidate_pair;
  const auto local = pair.local_candidate().type();
  const auto remote = pair.remote_candidate().type();
  path_ = (local == IceCandidateType::kRelay ||
           remote == IceCandidateType::kRelay)
              ? "relay"
              : (local == IceCandidateType::kSrflx ||
                 remote == IceCandidateType::kSrflx)
                    ? "srflx"
                    : "host";
  if (!event_) {
    return;
  }
  event_("media.path",
         {{"pc", name_},
          {"path", path_},
          {"local", std::string(webrtc::IceCandidateTypeToString(local)) +
                        " " + pair.local_candidate().address().ToString()},
          {"remote", std::string(webrtc::IceCandidateTypeToString(remote)) +
                         " " +
                         pair.remote_candidate().address().ToString()}});
}

void PeerConnection::OnConnectionChange(
    webrtc::PeerConnectionInterface::PeerConnectionState state) {
  if (!event_) {
    return;
  }
  event_("media.state",
         {{"pc", name_}, {"state", static_cast<int>(state)}});
  if (name_ != "p2p") {
    return;
  }
  const char* text = nullptr;
  switch (state) {
    case webrtc::PeerConnectionInterface::PeerConnectionState::kConnected:
      text = "connected";
      break;
    case webrtc::PeerConnectionInterface::PeerConnectionState::kConnecting:
      text = "connecting";
      break;
    case webrtc::PeerConnectionInterface::PeerConnectionState::kFailed:
      text = "failed";
      break;
    case webrtc::PeerConnectionInterface::PeerConnectionState::kClosed:
      text = "closed";
      break;
    default:
      break;
  }
  if (text == nullptr) {
    return;
  }
  Json data = {{"state", text}};
  if (state ==
      webrtc::PeerConnectionInterface::PeerConnectionState::kConnected) {
    data["path"] = path_.empty() ? "host" : path_;
  }
  event_("p2p.state", std::move(data));
}

void PeerConnection::OnTrack(
    webrtc::scoped_refptr<webrtc::RtpTransceiverInterface> transceiver) {
  if (!transceiver) {
    return;
  }
  auto track = transceiver->receiver()->track();
  if (track && track->kind() == "audio") {
    auto* audio = static_cast<webrtc::AudioTrackInterface*>(track.get());
    if (audio->GetSource()) {
      audio->GetSource()->SetVolume(gain_.load());
    }
    // P2P 期间 SFU 下行只暂停播放，保持 ICE/DTLS 不断，切回时直接复用。
    track->set_enabled(!receive_paused_.load());
  }
  if (event_) {
    event_("media.track", {{"pc", name_},
                            {"kind", track ? track->kind()
                                            : std::string("unknown")},
                            {"mid", transceiver->mid().value_or("")}});
  }
}

}  // namespace talk
