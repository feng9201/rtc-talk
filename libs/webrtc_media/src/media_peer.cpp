#include "media_peer.h"

#include "media_error.h"
#include "sdp_negotiator.h"

#include "rtc_base/logging.h"

#include <sstream>
#include <utility>

namespace rtcwin::webrtc_media {
namespace {

constexpr int kAudioMaxBitrateBps = 32000;
constexpr int kVideoMaxBitrateBps = 1200000;
constexpr int kVideoMaxFps = 15;

std::string EscapeJson(const std::string& value) {
  std::string result;
  result.reserve(value.size() + 8);
  for (char ch : value) {
    switch (ch) {
      case '"': result += "\\\""; break;
      case '\\': result += "\\\\"; break;
      case '\n': result += "\\n"; break;
      case '\r': result += "\\r"; break;
      case '\t': result += "\\t"; break;
      default: result += ch; break;
    }
  }
  return result;
}

const char* ConnectionStateName(
    webrtc::PeerConnectionInterface::PeerConnectionState state) {
  switch (state) {
    case webrtc::PeerConnectionInterface::PeerConnectionState::kNew:
      return "new";
    case webrtc::PeerConnectionInterface::PeerConnectionState::kConnecting:
      return "connecting";
    case webrtc::PeerConnectionInterface::PeerConnectionState::kConnected:
      return "connected";
    case webrtc::PeerConnectionInterface::PeerConnectionState::kDisconnected:
      return "disconnected";
    case webrtc::PeerConnectionInterface::PeerConnectionState::kFailed:
      return "failed";
    case webrtc::PeerConnectionInterface::PeerConnectionState::kClosed:
      return "closed";
  }
  return "unknown";
}

int ConnectionStateCode(
    webrtc::PeerConnectionInterface::PeerConnectionState state) {
  return static_cast<int>(state);
}

}  // namespace

MediaPeer::MediaPeer(std::string name, webrtc::Thread* signaling, Event event,
                     RemoteTrack remote_track)
    : name_(std::move(name)),
      signaling_(signaling),
      event_(std::move(event)),
      remote_track_(std::move(remote_track)) {}

MediaPeer::~MediaPeer() { Close(); }

bool MediaPeer::Create(
    webrtc::PeerConnectionFactoryInterface* factory,
    const webrtc::PeerConnectionInterface::RTCConfiguration& config,
    MediaError* error) {
  if (factory == nullptr || signaling_ == nullptr) {
    SetError(error, 1701, "PeerConnectionFactory 未初始化");
    return false;
  }
  const bool ok = RunOnSignaling([this, factory, &config] {
    if (pc_) return true;
    auto result = factory->CreatePeerConnectionOrError(
        config, webrtc::PeerConnectionDependencies(this));
    if (!result.ok()) return false;
    pc_ = result.MoveValue();
    return pc_ != nullptr;
  });
  if (!ok) SetError(error, 1702, "创建 PeerConnection 失败");
  return ok;
}

void MediaPeer::Close() {
  if (signaling_ == nullptr) return;
  RunOnSignaling([this] {
    if (!pc_) return;
    pc_->Close();
    pc_ = nullptr;
  });
  senders_.clear();
  state_.store(
      ConnectionStateCode(
          webrtc::PeerConnectionInterface::PeerConnectionState::kClosed),
      std::memory_order_relaxed);
}

bool MediaPeer::AddAudioTrack(
    webrtc::scoped_refptr<webrtc::AudioTrackInterface> track,
    webrtc::RtpTransceiverDirection direction, const std::string& stream_id,
    MediaError* error) {
  if (!track) {
    SetError(error, 1703, "音轨为空");
    return false;
  }
  const bool ok =
      RunOnSignaling([this, track = std::move(track), direction, stream_id]()
                         mutable {
                           if (!pc_) return false;
                           if (senders_.find(track->id()) != senders_.end()) {
                             return true;
                           }
                           if (pc_->remote_description() != nullptr) {
                             auto added = pc_->AddTrack(track, {stream_id});
                             if (!added.ok()) return false;
                             senders_[track->id()] = added.value();
                             return true;
                           }
                           webrtc::RtpTransceiverInit init;
                           init.direction = direction;
                           init.stream_ids = {stream_id};
                           webrtc::RtpEncodingParameters encoding;
                           encoding.max_bitrate_bps = kAudioMaxBitrateBps;
                           encoding.active = true;
                           init.send_encodings.push_back(encoding);
                           auto result = pc_->AddTransceiver(track, init);
                           if (!result.ok()) return false;
                           senders_[track->id()] = result.value()->sender();
                           return true;
                         });
  if (!ok) SetError(error, 1704, "添加音轨失败");
  return ok;
}

bool MediaPeer::AddVideoTrack(
    webrtc::scoped_refptr<webrtc::VideoTrackInterface> track,
    webrtc::RtpTransceiverDirection direction, const std::string& stream_id,
    MediaError* error) {
  if (!track) {
    SetError(error, 1705, "视频轨为空");
    return false;
  }
  const bool ok =
      RunOnSignaling([this, track = std::move(track), direction, stream_id]()
                         mutable {
                           if (!pc_) return false;
                           if (senders_.find(track->id()) != senders_.end()) {
                             return true;
                           }
                           if (pc_->remote_description() != nullptr) {
                             auto added = pc_->AddTrack(track, {stream_id});
                             if (!added.ok()) return false;
                             senders_[track->id()] = added.value();
                             return true;
                           }
                           webrtc::RtpTransceiverInit init;
                           init.direction = direction;
                           init.stream_ids = {stream_id};
                           webrtc::RtpEncodingParameters encoding;
                           encoding.max_bitrate_bps = kVideoMaxBitrateBps;
                           encoding.max_framerate = kVideoMaxFps;
                           encoding.active = false;
                           init.send_encodings.push_back(encoding);
                           auto result = pc_->AddTransceiver(track, init);
                           if (!result.ok()) return false;
                           senders_[track->id()] = result.value()->sender();
                           return true;
                         });
  if (!ok) SetError(error, 1706, "添加视频轨失败");
  return ok;
}

bool MediaPeer::CreateAndSetLocal(bool offer, MediaError* error) {
  return CreateAndSetLocalDescription(signaling_, pc_, offer, error);
}

bool MediaPeer::SetRemote(webrtc::SdpType type, const std::string& sdp,
                          MediaError* error) {
  return SetRemoteDescription(signaling_, pc_, type, sdp, error);
}

bool MediaPeer::AddIceCandidate(const std::string& candidate,
                                const std::string& mid, int mline_index,
                                MediaError* error) {
  if (candidate.empty()) return true;
  auto ice = webrtc::IceCandidate::Create(mid, mline_index, candidate);
  if (!ice) {
    SetError(error, 1713, "候选地址无效");
    return false;
  }
  const bool ok = RunOnSignaling([this, &ice] {
    return pc_ != nullptr && pc_->AddIceCandidate(ice.get());
  });
  if (!ok) SetError(error, 1714, "添加 ICE 候选失败");
  return ok;
}

bool MediaPeer::GetLocalSdp(std::string* sdp, MediaError* error) const {
  const bool ok = RunOnSignaling([this, sdp] {
    if (!pc_ || pc_->local_description() == nullptr) return false;
    return pc_->local_description()->ToString(sdp);
  });
  if (!ok) SetError(error, 1715, "序列化本地 SDP 失败");
  return ok;
}

bool MediaPeer::GetAudioMid(std::string* mid) const {
  return RunOnSignaling([this, mid] {
    if (!pc_ || mid == nullptr) return false;
    for (const auto& transceiver : pc_->GetTransceivers()) {
      if (transceiver &&
          transceiver->media_type() == webrtc::MediaType::AUDIO &&
          transceiver->mid().has_value()) {
        *mid = *transceiver->mid();
        return true;
      }
    }
    return false;
  });
}

bool MediaPeer::GetVideoMid(std::string* mid) const {
  return RunOnSignaling([this, mid] {
    if (!pc_ || mid == nullptr) return false;
    for (const auto& transceiver : pc_->GetTransceivers()) {
      if (transceiver &&
          transceiver->media_type() == webrtc::MediaType::VIDEO &&
          transceiver->mid().has_value()) {
        *mid = *transceiver->mid();
        return true;
      }
    }
    return false;
  });
}

bool MediaPeer::HasAudioSender() const {
  return RunOnSignaling([this] {
    if (!pc_) return false;
    for (const auto& transceiver : pc_->GetTransceivers()) {
      if (transceiver &&
          transceiver->media_type() == webrtc::MediaType::AUDIO &&
          transceiver->sender()) {
        return true;
      }
    }
    return false;
  });
}

bool MediaPeer::AudioSenderActive() const {
  return RunOnSignaling([this] {
    if (!pc_) return false;
    for (const auto& transceiver : pc_->GetTransceivers()) {
      if (!transceiver ||
          transceiver->media_type() != webrtc::MediaType::AUDIO ||
          !transceiver->sender()) {
        continue;
      }
      const auto parameters = transceiver->sender()->GetParameters();
      return !parameters.encodings.empty() &&
             parameters.encodings.front().active;
    }
    return false;
  });
}

bool MediaPeer::SetAudioSenderActive(bool active) {
  return RunOnSignaling([this, active] {
    if (!pc_) return false;
    for (const auto& transceiver : pc_->GetTransceivers()) {
      if (!transceiver ||
          transceiver->media_type() != webrtc::MediaType::AUDIO) {
        continue;
      }
      auto sender = transceiver->sender();
      if (!sender) continue;
      auto parameters = sender->GetParameters();
      for (auto& encoding : parameters.encodings) {
        encoding.active = active;
        encoding.max_bitrate_bps = kAudioMaxBitrateBps;
      }
      if (!sender->SetParameters(parameters).ok()) return false;
    }
    return true;
  });
}

bool MediaPeer::HasVideoSender() const {
  return RunOnSignaling([this] {
    if (!pc_) return false;
    for (const auto& transceiver : pc_->GetTransceivers()) {
      if (transceiver &&
          transceiver->media_type() == webrtc::MediaType::VIDEO &&
          transceiver->sender()) {
        return true;
      }
    }
    return false;
  });
}

bool MediaPeer::VideoSenderActive(const std::string& mid) const {
  return RunOnSignaling([this, mid] {
    if (!pc_) return false;
    for (const auto& transceiver : pc_->GetTransceivers()) {
      if (!transceiver ||
          transceiver->media_type() != webrtc::MediaType::VIDEO ||
          !transceiver->sender() || !transceiver->mid().has_value() ||
          *transceiver->mid() != mid) {
        continue;
      }
      const auto parameters = transceiver->sender()->GetParameters();
      return !parameters.encodings.empty() &&
             parameters.encodings.front().active;
    }
    return false;
  });
}

bool MediaPeer::SetVideoSenderActive(const std::string& mid, bool active) {
  return RunOnSignaling([this, mid, active] {
    if (!pc_) return false;
    for (const auto& transceiver : pc_->GetTransceivers()) {
      if (!transceiver ||
          transceiver->media_type() != webrtc::MediaType::VIDEO ||
          !transceiver->sender() || !transceiver->mid().has_value() ||
          *transceiver->mid() != mid) {
        continue;
      }
      auto parameters = transceiver->sender()->GetParameters();
      for (auto& encoding : parameters.encodings) {
        encoding.active = active;
        encoding.max_bitrate_bps = kVideoMaxBitrateBps;
        encoding.max_framerate = kVideoMaxFps;
      }
      return transceiver->sender()->SetParameters(parameters).ok();
    }
    return false;
  });
}

bool MediaPeer::CollectMediaStats(PeerMediaStats* out) const {
  return CollectPeerMediaStats(signaling_, pc_, out);
}

int MediaPeer::state() const {
  return state_.load(std::memory_order_relaxed);
}

std::string MediaPeer::state_name() const {
  return ConnectionStateName(
      static_cast<webrtc::PeerConnectionInterface::PeerConnectionState>(
          state()));
}

void MediaPeer::OnSignalingChange(
    webrtc::PeerConnectionInterface::SignalingState) {}

void MediaPeer::OnDataChannel(
    webrtc::scoped_refptr<webrtc::DataChannelInterface>) {}

void MediaPeer::OnIceCandidate(const webrtc::IceCandidate* candidate) {
  if (!event_) return;
  std::ostringstream data;
  data << "{\"pc\":\"" << EscapeJson(name_) << "\",\"candidate\":\""
       << EscapeJson(candidate != nullptr ? candidate->ToString() : "")
       << "\"";
  if (candidate != nullptr) {
    data << ",\"sdp_mid\":\"" << EscapeJson(candidate->sdp_mid())
         << "\",\"sdp_mline_index\":" << candidate->sdp_mline_index();
  }
  data << "}";
  event_("ice.candidate", data.str());
}

void MediaPeer::OnIceGatheringChange(
    webrtc::PeerConnectionInterface::IceGatheringState) {}

void MediaPeer::OnIceSelectedCandidatePairChanged(
    const webrtc::CandidatePairChangeEvent&) {}

void MediaPeer::OnConnectionChange(
    webrtc::PeerConnectionInterface::PeerConnectionState state) {
  ReportConnectionState(state);
}

void MediaPeer::OnStandardizedIceConnectionChange(
    webrtc::PeerConnectionInterface::IceConnectionState state) {
  using PcState = webrtc::PeerConnectionInterface::PeerConnectionState;
  PcState mapped = PcState::kConnecting;
  switch (state) {
    case webrtc::PeerConnectionInterface::kIceConnectionNew:
      mapped = PcState::kNew;
      break;
    case webrtc::PeerConnectionInterface::kIceConnectionChecking:
      mapped = PcState::kConnecting;
      break;
    case webrtc::PeerConnectionInterface::kIceConnectionConnected:
    case webrtc::PeerConnectionInterface::kIceConnectionCompleted:
      mapped = PcState::kConnected;
      break;
    case webrtc::PeerConnectionInterface::kIceConnectionDisconnected:
      mapped = PcState::kDisconnected;
      break;
    case webrtc::PeerConnectionInterface::kIceConnectionFailed:
      mapped = PcState::kFailed;
      break;
    case webrtc::PeerConnectionInterface::kIceConnectionClosed:
      mapped = PcState::kClosed;
      break;
    case webrtc::PeerConnectionInterface::kIceConnectionMax:
      break;
  }
  ReportConnectionState(mapped);
}

void MediaPeer::OnTrack(
    webrtc::scoped_refptr<webrtc::RtpTransceiverInterface> transceiver) {
  if (!transceiver || !transceiver->receiver()) return;
  auto track = transceiver->receiver()->track();
  const std::string mid = transceiver->mid().value_or("");
  if (!track) return;
  if (track->kind() == "audio") {
    auto audio = webrtc::scoped_refptr<webrtc::AudioTrackInterface>(
        static_cast<webrtc::AudioTrackInterface*>(track.get()));
    if (remote_track_) {
      remote_track_(name_, mid, "audio", audio,
                    webrtc::scoped_refptr<webrtc::VideoTrackInterface>());
    }
  } else if (track->kind() == "video") {
    auto video = webrtc::scoped_refptr<webrtc::VideoTrackInterface>(
        static_cast<webrtc::VideoTrackInterface*>(track.get()));
    if (remote_track_) {
      remote_track_(
          name_, mid, "video",
          webrtc::scoped_refptr<webrtc::AudioTrackInterface>(), video);
    }
  } else {
    return;
  }
  if (event_) {
    event_("media.track",
           "{\"pc\":\"" + EscapeJson(name_) + "\",\"kind\":\"" +
               EscapeJson(track->kind()) + "\",\"mid\":\"" +
               EscapeJson(mid) + "\"}");
  }
}

void MediaPeer::ReportConnectionState(
    webrtc::PeerConnectionInterface::PeerConnectionState state) {
  const int code = ConnectionStateCode(state);
  state_.store(code, std::memory_order_relaxed);
  if (reported_state_.exchange(code, std::memory_order_relaxed) == code ||
      !event_) {
    return;
  }
  std::ostringstream data;
  data << "{\"pc\":\"" << EscapeJson(name_) << "\",\"state\":\""
       << ConnectionStateName(state) << "\"}";
  event_("audio.connection", data.str());
}

}  // namespace rtcwin::webrtc_media
