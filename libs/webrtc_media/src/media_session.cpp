#include "rtcwin/webrtc_media/media_session.h"
#include "rtcwin/webrtc_media/switchable_audio_device.h"

#include "remote_video_sink.h"
#include "video/webrtc_h264_factory.h"
#include "video_track_source.h"

#include "api/audio/audio_frame.h"
#include "api/audio/audio_frame_processor.h"
#include "api/audio/audio_processing.h"
#include "api/audio/builtin_audio_processing_builder.h"
#include "api/audio/create_audio_device_module.h"
#include "api/audio_options.h"
#include "api/audio_codecs/audio_decoder_factory_template.h"
#include "api/audio_codecs/audio_encoder_factory_template.h"
#include "api/audio_codecs/opus/audio_decoder_opus.h"
#include "api/audio_codecs/opus/audio_encoder_opus.h"
#include "api/create_modular_peer_connection_factory.h"
#include "api/enable_media.h"
#include "api/environment/environment_factory.h"
#include "api/jsep.h"
#include "api/make_ref_counted.h"
#include "api/media_stream_interface.h"
#include "api/peer_connection_interface.h"
#include "api/rtp_transceiver_interface.h"
#include "api/stats/rtc_stats_collector_callback.h"
#include "api/stats/rtc_stats_report.h"
#include "api/stats/rtcstats_objects.h"
#include "rtc_base/logging.h"
#include "rtc_base/thread.h"
#include "rtc_base/win/scoped_com_initializer.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <memory>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace rtcwin::webrtc_media {
namespace {

constexpr int kAudioMaxBitrateBps = 32000;
constexpr int kVideoMaxBitrateBps = 1200000;
constexpr int kVideoMaxFps = 15;
constexpr auto kSdpTimeout = std::chrono::seconds(5);

void SetError(MediaError* error, int code, std::string message) {
  if (error == nullptr) return;
  error->code = code;
  error->message = std::move(message);
}

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

class CaptureMonitor final : public webrtc::AudioTrackSinkInterface {
 public:
  void OnData(const void*, int, int sample_rate, size_t channels,
              size_t frames) override {
    (void)sample_rate;
    (void)channels;
    frames_.fetch_add(frames, std::memory_order_relaxed);
  }

  std::uint64_t frames() const {
    return frames_.load(std::memory_order_relaxed);
  }

 private:
  std::atomic<std::uint64_t> frames_{0};
};

class CaptureGate final : public webrtc::AudioFrameProcessor {
 public:
  void Process(std::unique_ptr<webrtc::AudioFrame> frame) override {
    if (!frame) return;
    capture_frames_.fetch_add(1, std::memory_order_relaxed);
    if (muted_.load(std::memory_order_relaxed)) frame->Mute();
    OnAudioFrameCallback sink;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      sink = sink_;
    }
    if (sink) sink(std::move(frame));
  }

  void SetSink(OnAudioFrameCallback sink) override {
    std::lock_guard<std::mutex> lock(mutex_);
    sink_ = std::move(sink);
  }

  void SetMuted(bool muted) { muted_.store(muted, std::memory_order_relaxed); }
  std::uint64_t frames() const {
    return capture_frames_.load(std::memory_order_relaxed);
  }

 private:
  std::atomic<bool> muted_{true};
  std::atomic<std::uint64_t> capture_frames_{0};
  std::mutex mutex_;
  OnAudioFrameCallback sink_;
};

struct CreateDescriptionState {
  std::mutex mutex;
  std::condition_variable cv;
  bool done = false;
  std::unique_ptr<webrtc::SessionDescriptionInterface> description;
};

class CreateDescriptionObserver
    : public webrtc::CreateSessionDescriptionObserver {
 public:
  explicit CreateDescriptionObserver(
      std::shared_ptr<CreateDescriptionState> state)
      : state_(std::move(state)) {}

  void OnSuccess(webrtc::SessionDescriptionInterface* description) override {
    std::lock_guard<std::mutex> lock(state_->mutex);
    state_->description.reset(description);
    state_->done = true;
    state_->cv.notify_all();
  }

  void OnFailure(webrtc::RTCError) override {
    std::lock_guard<std::mutex> lock(state_->mutex);
    state_->description.reset();
    state_->done = true;
    state_->cv.notify_all();
  }

 private:
  std::shared_ptr<CreateDescriptionState> state_;
};

struct SetDescriptionState {
  std::mutex mutex;
  std::condition_variable cv;
  bool done = false;
  bool ok = false;
};

class SetDescriptionObserver : public webrtc::SetSessionDescriptionObserver {
 public:
  explicit SetDescriptionObserver(std::shared_ptr<SetDescriptionState> state)
      : state_(std::move(state)) {}

  void OnSuccess() override {
    std::lock_guard<std::mutex> lock(state_->mutex);
    state_->ok = true;
    state_->done = true;
    state_->cv.notify_all();
  }

  void OnFailure(webrtc::RTCError) override {
    std::lock_guard<std::mutex> lock(state_->mutex);
    state_->ok = false;
    state_->done = true;
    state_->cv.notify_all();
  }

 private:
  std::shared_ptr<SetDescriptionState> state_;
};

std::unique_ptr<webrtc::SessionDescriptionInterface> WaitForDescription(
    const std::shared_ptr<CreateDescriptionState>& state) {
  std::unique_lock<std::mutex> lock(state->mutex);
  if (!state->cv.wait_for(lock, kSdpTimeout, [&] { return state->done; })) {
    return nullptr;
  }
  return std::move(state->description);
}

bool WaitForSetDescription(const std::shared_ptr<SetDescriptionState>& state) {
  std::unique_lock<std::mutex> lock(state->mutex);
  return state->cv.wait_for(lock, kSdpTimeout, [&] { return state->done; }) &&
         state->ok;
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
class MediaPeer final : public webrtc::PeerConnectionObserver {
 public:
  using Event = std::function<void(std::string, std::string)>;
  using RemoteTrack = std::function<void(
      const std::string&, const std::string&, const std::string&,
      webrtc::scoped_refptr<webrtc::AudioTrackInterface>,
      webrtc::scoped_refptr<webrtc::VideoTrackInterface>)>;

  MediaPeer(std::string name, webrtc::Thread* signaling, Event event,
            RemoteTrack remote_track)
      : name_(std::move(name)),
        signaling_(signaling),
        event_(std::move(event)),
        remote_track_(std::move(remote_track)) {}

  ~MediaPeer() override { Close(); }

  bool Create(webrtc::PeerConnectionFactoryInterface* factory,
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

  void Close() {
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

  bool AddAudioTrack(webrtc::scoped_refptr<webrtc::AudioTrackInterface> track,
                webrtc::RtpTransceiverDirection direction,
                const std::string& stream_id, MediaError* error) {
    if (!track) {
      SetError(error, 1703, "音轨为空");
      return false;
    }
    const bool ok = RunOnSignaling([this, track = std::move(track), direction,
                                    stream_id]() mutable {
      if (!pc_) return false;
      if (senders_.find(track->id()) != senders_.end()) return true;
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

  bool AddVideoTrack(webrtc::scoped_refptr<webrtc::VideoTrackInterface> track,
                     webrtc::RtpTransceiverDirection direction,
                     const std::string& stream_id, MediaError* error) {
    if (!track) {
      SetError(error, 1705, "视频轨为空");
      return false;
    }
    const bool ok = RunOnSignaling([this, track = std::move(track), direction,
                                    stream_id]() mutable {
      if (!pc_) return false;
      if (senders_.find(track->id()) != senders_.end()) return true;
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

  bool CreateAndSetLocal(bool offer, MediaError* error) {
    auto create_state = std::make_shared<CreateDescriptionState>();
    auto observer = webrtc::make_ref_counted<CreateDescriptionObserver>(
        create_state);
    const bool requested = RunOnSignaling([this, offer, &observer] {
      if (!pc_) return false;
      webrtc::PeerConnectionInterface::RTCOfferAnswerOptions options;
      if (offer) {
        pc_->CreateOffer(observer.get(), options);
      } else {
        pc_->CreateAnswer(observer.get(), options);
      }
      return true;
    });
    if (!requested) {
      SetError(error, 1705, "PeerConnection 尚未创建");
      return false;
    }
    auto description = WaitForDescription(create_state);
    if (!description) {
      SetError(error, 1706, offer ? "生成音频 offer 超时" : "生成音频 answer 超时");
      return false;
    }
    auto set_state = std::make_shared<SetDescriptionState>();
    auto set_observer = webrtc::make_ref_counted<SetDescriptionObserver>(
        set_state);
    auto* raw = description.release();
    const bool posted = RunOnSignaling([this, &set_observer, raw] {
      if (!pc_) {
        delete raw;
        return false;
      }
      pc_->SetLocalDescription(set_observer.get(), raw);
      return true;
    });
    if (!posted) {
      SetError(error, 1707, "设置本地 SDP 失败");
      return false;
    }
    if (!WaitForSetDescription(set_state)) {
      SetError(error, 1708, "应用本地 SDP 失败");
      return false;
    }
    return true;
  }

  bool SetRemote(webrtc::SdpType type, const std::string& sdp,
                 MediaError* error) {
    if (sdp.empty()) {
      SetError(error, 1709, "远端 SDP 为空");
      return false;
    }
    auto description = webrtc::CreateSessionDescription(type, sdp);
    if (!description) {
      SetError(error, 1710, "解析远端 SDP 失败");
      return false;
    }
    auto set_state = std::make_shared<SetDescriptionState>();
    auto observer = webrtc::make_ref_counted<SetDescriptionObserver>(
        set_state);
    auto* raw = description.release();
    const bool posted = RunOnSignaling([this, &observer, raw] {
      if (!pc_) {
        delete raw;
        return false;
      }
      pc_->SetRemoteDescription(observer.get(), raw);
      return true;
    });
    if (!posted) {
      SetError(error, 1711, "PeerConnection 尚未创建");
      return false;
    }
    if (!WaitForSetDescription(set_state)) {
      SetError(error, 1712, "应用远端 SDP 失败");
      return false;
    }
    return true;
  }

  bool AddIceCandidate(const std::string& candidate, const std::string& mid,
                       int mline_index, MediaError* error) {
    if (candidate.empty()) return true;
    auto ice = webrtc::IceCandidate::Create(mid, mline_index, candidate);
    if (!ice) {
      SetError(error, 1713, "候选地址无效");
      return false;
    }
    const bool ok = RunOnSignaling(
        [this, &ice] { return pc_ != nullptr && pc_->AddIceCandidate(ice.get()); });
    if (!ok) SetError(error, 1714, "添加 ICE 候选失败");
    return ok;
  }

  bool GetLocalSdp(std::string* sdp, MediaError* error) const {
    const bool ok = RunOnSignaling([this, sdp] {
      if (!pc_ || pc_->local_description() == nullptr) return false;
      return pc_->local_description()->ToString(sdp);
    });
    if (!ok) SetError(error, 1715, "序列化本地 SDP 失败");
    return ok;
  }

  bool GetAudioMid(std::string* mid) const {
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

  bool GetVideoMid(std::string* mid) const {
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

  bool HasAudioSender() const {
    return RunOnSignaling([this] {
      if (!pc_) return false;
      for (const auto& transceiver : pc_->GetTransceivers()) {
        if (transceiver && transceiver->media_type() ==
                               webrtc::MediaType::AUDIO &&
            transceiver->sender()) {
          return true;
        }
      }
      return false;
    });
  }

  bool AudioSenderActive() const {
    return RunOnSignaling([this] {
      if (!pc_) return false;
      for (const auto& transceiver : pc_->GetTransceivers()) {
        if (!transceiver || transceiver->media_type() !=
                                webrtc::MediaType::AUDIO ||
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

  bool SetAudioSenderActive(bool active) {
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

  bool HasVideoSender() const {
    return RunOnSignaling([this] {
      if (!pc_) return false;
      for (const auto& transceiver : pc_->GetTransceivers()) {
        if (transceiver && transceiver->media_type() ==
                               webrtc::MediaType::VIDEO &&
            transceiver->sender()) {
          return true;
        }
      }
      return false;
    });
  }

  bool VideoSenderActive(const std::string& mid) const {
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

  bool SetVideoSenderActive(const std::string& mid, bool active) {
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

  bool CollectMediaStats(PeerMediaStats* out) const {
    if (out == nullptr) return false;
    *out = {};
    auto state = std::make_shared<StatsState>();
    auto observer = webrtc::make_ref_counted<StatsObserver>(state);
    const bool requested = RunOnSignaling([this, &observer] {
      if (!pc_) return false;
      pc_->GetStats(observer.get());
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
  int state() const { return state_.load(std::memory_order_relaxed); }
  std::string state_name() const {
    return ConnectionStateName(
        static_cast<webrtc::PeerConnectionInterface::PeerConnectionState>(
            state()));
  }

 private:
  template <class F>
  auto RunOnSignaling(F&& f) const {
    return signaling_->BlockingCall(std::forward<F>(f));
  }

  void OnSignalingChange(
      webrtc::PeerConnectionInterface::SignalingState) override {}

  void OnDataChannel(
      webrtc::scoped_refptr<webrtc::DataChannelInterface>) override {}

  void OnIceCandidate(const webrtc::IceCandidate* candidate) override {
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

  void OnIceGatheringChange(
      webrtc::PeerConnectionInterface::IceGatheringState) override {}

  void OnIceSelectedCandidatePairChanged(
      const webrtc::CandidatePairChangeEvent&) override {}

  void OnConnectionChange(
      webrtc::PeerConnectionInterface::PeerConnectionState state) override {
    ReportConnectionState(state);
  }

  void OnStandardizedIceConnectionChange(
      webrtc::PeerConnectionInterface::IceConnectionState state) override {
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

  void OnTrack(webrtc::scoped_refptr<webrtc::RtpTransceiverInterface>
                   transceiver) override {
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
        remote_track_(name_, mid, "video",
                      webrtc::scoped_refptr<webrtc::AudioTrackInterface>(),
                      video);
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

  std::string name_;
  webrtc::Thread* signaling_ = nullptr;
  Event event_;
  RemoteTrack remote_track_;
  webrtc::scoped_refptr<webrtc::PeerConnectionInterface> pc_;
  std::map<std::string, webrtc::scoped_refptr<webrtc::RtpSenderInterface>>
      senders_;
  std::atomic<int> state_{0};
  std::atomic<int> reported_state_{0};

  void ReportConnectionState(
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
};

bool SelectDevice(webrtc::AudioDeviceModule* adm, bool recording,
                  const std::string& wanted, std::string* selected,
                  std::string* error) {
  if (adm == nullptr) {
    if (error) *error = "音频设备模块为空";
    return false;
  }
  int found_index = -1;
  int fallback_index = -1;
  const int count = recording ? adm->RecordingDevices() : adm->PlayoutDevices();
  for (int index = 0; index < count; ++index) {
    char name[webrtc::kAdmMaxDeviceNameSize]{};
    char guid[webrtc::kAdmMaxGuidSize]{};
    const int status = recording ? adm->RecordingDeviceName(index, name, guid)
                                 : adm->PlayoutDeviceName(index, name, guid);
    if (status != 0) continue;
    if (fallback_index < 0) fallback_index = index;
    if (!wanted.empty() && (wanted == guid || wanted == name)) {
      found_index = index;
      break;
    }
  }
  if (found_index < 0) found_index = fallback_index;
  if (found_index < 0) {
    if (error) {
      *error = recording ? "未检测到麦克风设备" : "未检测到播放设备";
    }
    return false;
  }
  const auto slot = static_cast<uint16_t>(std::min(found_index, 65535));
  int status = recording ? adm->SetRecordingDevice(slot)
                         : adm->SetPlayoutDevice(slot);
  if (status != 0) {
    status = recording
                 ? adm->SetRecordingDevice(
                       webrtc::AudioDeviceModule::kDefaultCommunicationDevice)
                 : adm->SetPlayoutDevice(
                       webrtc::AudioDeviceModule::kDefaultCommunicationDevice);
    if (status != 0) {
      if (error) *error = recording ? "选择麦克风失败" : "选择播放设备失败";
      return false;
    }
  }
  char name[webrtc::kAdmMaxDeviceNameSize]{};
  char guid[webrtc::kAdmMaxGuidSize]{};
  const int name_status =
      recording ? adm->RecordingDeviceName(slot, name, guid)
                : adm->PlayoutDeviceName(slot, name, guid);
  if (selected) {
    *selected = name_status == 0 && name[0] != '\0' ? name : "default";
  }
  return true;
}

}  // namespace

struct MediaSession::Impl {
  ~Impl() { Shutdown(); }

  bool Initialize(const MediaConfig& next, MediaSession::Event cb,
                  MediaSession::RemoteVideoCallback remote_video,
                  MediaError* error) {
    if (initialized) {
      SetError(error, 1700, "音频媒体会话已初始化");
      return false;
    }
    if (next.audio_backend != "wasapi" && next.audio_backend != "default" &&
        next.audio_backend != "fake") {
      SetError(error, 1701, "不支持的音频后端");
      return false;
    }
    if (next.audio_source != "mic" && next.audio_source != "system") {
      SetError(error, 1702, "audio_source 必须是 mic/system");
      return false;
    }
    if (next.audio_backend == "fake" && next.audio_source == "system") {
      SetError(error, 1702, "fake 后端不支持系统音采集");
      return false;
    }
    config = next;
    event = std::move(cb);
    {
      std::lock_guard<std::mutex> lock(remote_mutex);
      remote_video_callback = std::move(remote_video);
    }
    fake_device = next.audio_backend == "fake";
    char* webrtc_log = nullptr;
    size_t webrtc_log_length = 0;
    if (_dupenv_s(&webrtc_log, &webrtc_log_length,
                  "RTCWIN_WEBRTC_LOG") == 0 &&
        webrtc_log != nullptr) {
      const std::string enabled(webrtc_log);
      free(webrtc_log);
      if (enabled == "1" || enabled == "true" || enabled == "yes" ||
          enabled == "on") {
        webrtc::LogMessage::LogToDebug(webrtc::LS_INFO);
        webrtc::LogMessage::LogTimestamps();
        webrtc::LogMessage::LogThreads();
      }
    }
    volume.store(std::clamp(next.volume, 0, 100) / 100.0,
                 std::memory_order_relaxed);
    muted.store(next.muted, std::memory_order_relaxed);

    network = webrtc::Thread::CreateWithSocketServer();
    worker = webrtc::Thread::Create();
    signaling = webrtc::Thread::Create();
    if (!network || !worker || !signaling) {
      SetError(error, 1703, "创建 WebRTC 线程失败");
      Shutdown();
      return false;
    }
    network->SetName("rtcwin-media-network", nullptr);
    worker->SetName("rtcwin-media-worker", nullptr);
    signaling->SetName("rtcwin-media-signaling", nullptr);
    if (!network->Start() || !worker->Start() || !signaling->Start()) {
      SetError(error, 1704, "启动 WebRTC 线程失败");
      Shutdown();
      return false;
    }

    const webrtc::Environment environment = webrtc::CreateEnvironment();
    const auto layer = next.audio_backend == "fake"
                           ? webrtc::AudioDeviceModule::kDummyAudio
                           : webrtc::AudioDeviceModule::kWindowsCoreAudio;
    const bool system_audio = next.audio_source == "system";
    std::string device_error;
    const bool devices_ok =
        worker->BlockingCall([this, &environment, layer, system_audio,
                              &device_error] {
          if (layer == webrtc::AudioDeviceModule::kWindowsCoreAudio) {
            com = std::make_unique<webrtc::ScopedCOMInitializer>(
                webrtc::ScopedCOMInitializer::kMTA);
            if (!com->Succeeded()) {
              device_error = "COM MTA 初始化失败";
              return false;
            }
          }
          auto core_adm = webrtc::CreateAudioDeviceModule(environment, layer);
          if (!core_adm) {
            device_error = "音频设备模块初始化失败";
            return false;
          }
          if (layer == webrtc::AudioDeviceModule::kWindowsCoreAudio) {
            switchable = webrtc::make_ref_counted<SwitchableAudioDevice>(
                core_adm, system_audio);
            adm = switchable;
          } else {
            adm = core_adm;
          }
          if (adm->Init() != 0) {
            device_error = "音频设备模块初始化失败";
            return false;
          }
          if (layer == webrtc::AudioDeviceModule::kDummyAudio) {
            recording_name = "fake-microphone";
            playout_name = "fake-speaker";
            return true;
          }
          if (system_audio) {
            recording_name = "system-default-render";
          } else {
            if (!SelectDevice(adm.get(), true, config.recording_device,
                              &recording_name, &device_error)) {
              return false;
            }
          }
          if (!SelectDevice(adm.get(), false, config.playout_device,
                            &playout_name, &device_error)) {
            return false;
          }
          return true;
        });
    if (!devices_ok) {
      SetError(error, 1705,
               device_error.empty() ? "音频设备初始化失败" : device_error);
      Shutdown();
      return false;
    }

    auto gate = std::make_unique<CaptureGate>();
    gate->SetMuted(muted.load(std::memory_order_relaxed));
    capture_gate = gate.get();

    webrtc::PeerConnectionFactoryDependencies dependencies;
    dependencies.network_thread = network.get();
    dependencies.worker_thread = worker.get();
    dependencies.signaling_thread = signaling.get();
    dependencies.env = environment;
    dependencies.adm = adm;

    webrtc::AudioProcessing::Config apm_config;
    const bool real_microphone =
        next.audio_backend != "fake" && !system_audio;
    apm_config.echo_canceller.enabled = real_microphone;
    apm_config.noise_suppression.enabled = real_microphone;
    apm_config.gain_controller2.enabled = real_microphone;
    apm_config.high_pass_filter.enabled = real_microphone;
    dependencies.audio_processing_builder =
        std::make_unique<webrtc::BuiltinAudioProcessingBuilder>(apm_config);
    dependencies.audio_encoder_factory =
        webrtc::CreateAudioEncoderFactory<webrtc::AudioEncoderOpus>();
    dependencies.audio_decoder_factory =
        webrtc::CreateAudioDecoderFactory<webrtc::AudioDecoderOpus>();
    dependencies.audio_frame_processor = std::move(gate);
    dependencies.video_encoder_factory = CreateMfH264EncoderFactory();
    dependencies.video_decoder_factory = CreateMfH264DecoderFactory();
    webrtc::EnableMedia(dependencies);
    factory = webrtc::CreateModularPeerConnectionFactory(
        std::move(dependencies));
    if (!factory) {
      SetError(error, 1706, "创建 PeerConnectionFactory 失败");
      Shutdown();
      return false;
    }

    const bool track_ok = signaling->BlockingCall([this] {
      auto source = factory->CreateAudioSource(webrtc::AudioOptions());
      if (!source) return false;
      capture_monitor = std::make_unique<CaptureMonitor>();
      source->AddSink(capture_monitor.get());
      audio_track = factory->CreateAudioTrack(
          config.audio_source == "system" ? "rtcwin-system" : "rtcwin-mic",
          source.get());
      if (!audio_track) return false;
      audio_track->set_enabled(true);
      camera_source = webrtc::make_ref_counted<CameraVideoTrackSource>();
      camera_track = factory->CreateVideoTrack(camera_source, "rtcwin-camera");
      if (!camera_track) return false;
      camera_track->set_enabled(true);
      return true;
    });
    if (!track_ok) {
      SetError(error, 1707, "创建本地音视频轨失败");
      Shutdown();
      return false;
    }

    rtc_config.sdp_semantics = webrtc::SdpSemantics::kUnifiedPlan;
    for (const auto& item : next.ice_servers) {
      webrtc::PeerConnectionInterface::IceServer server;
      server.urls = item.urls;
      server.username = item.username;
      server.password = item.password;
      rtc_config.servers.push_back(std::move(server));
    }

    initialized = true;
    return true;
  }

  void Shutdown() {
    if (shutdown) return;
    shutdown = true;
    ClosePcs();
    if (audio_track && signaling) {
      signaling->BlockingCall([this] {
        if (capture_monitor) {
          if (auto* source = audio_track->GetSource()) {
            source->RemoveSink(capture_monitor.get());
          }
        }
        audio_track->set_enabled(false);
      });
    }
    audio_track = nullptr;
    camera_track = nullptr;
    camera_source = nullptr;
    capture_monitor.reset();
    capture_gate = nullptr;
    {
      std::lock_guard<std::mutex> lock(remote_mutex);
      remote_video_callback = nullptr;
    }
    factory = nullptr;
    if (worker) {
      worker->BlockingCall([this] {
        if (adm) {
          if (adm->Playing()) adm->StopPlayout();
          if (adm->Recording()) adm->StopRecording();
          adm->Terminate();
          // ADM and its AudioDeviceBuffer enforce thread affinity. Release the
          // last reference on the worker thread that created it.
          adm = nullptr;
          switchable = nullptr;
        }
        com.reset();
      });
    } else {
      adm = nullptr;
      switchable = nullptr;
      com.reset();
    }
    signaling.reset();
    worker.reset();
    network.reset();
    initialized = false;
  }

  void ClosePcs() {
    if (p2p) {
      p2p->Close();
      p2p.reset();
    }
    if (sfu_recv) {
      sfu_recv->Close();
      sfu_recv.reset();
    }
    if (sfu_send) {
      sfu_send->Close();
      sfu_send.reset();
    }
    std::lock_guard<std::mutex> lock(remote_mutex);
    remote_tracks.clear();
    remote_video_tracks.clear();
    remote_track_info.clear();
  }

  bool EnsureSfuSend(MediaError* error) {
    if (!initialized) {
      SetError(error, 1710, "音频媒体会话未初始化");
      return false;
    }
    if (!sfu_send) {
      sfu_send = std::make_unique<MediaPeer>("sfu-send", signaling.get(), event,
                                             RemoteCallback());
      if (!sfu_send->Create(factory.get(), rtc_config, error)) return false;
      std::string playout_error;
      if (!EnsurePlayout(&playout_error)) {
        SetError(error, 1708, playout_error);
        return false;
      }
    }
    return true;
  }

  bool EnsureSfuRecv(MediaError* error) {
    if (!initialized) {
      SetError(error, 1711, "音频媒体会话未初始化");
      return false;
    }
    if (!sfu_recv) {
      sfu_recv = std::make_unique<MediaPeer>("sfu-recv", signaling.get(), event,
                                             RemoteCallback());
      if (!sfu_recv->Create(factory.get(), rtc_config, error)) return false;
      std::string playout_error;
      if (!EnsurePlayout(&playout_error)) {
        SetError(error, 1708, playout_error);
        return false;
      }
    }
    return true;
  }

  MediaPeer::RemoteTrack RemoteCallback() {
    return [this](const std::string& pc, const std::string& mid,
                  const std::string& kind,
                  webrtc::scoped_refptr<webrtc::AudioTrackInterface> audio,
                  webrtc::scoped_refptr<webrtc::VideoTrackInterface> video) {
      if (kind == "audio" && audio) {
        ApplyRemoteTrackState(audio, pc);
        std::lock_guard<std::mutex> lock(remote_mutex);
        for (auto& entry : remote_tracks) {
          if (entry.pc == pc && entry.mid == mid) {
            entry.track = audio;
            return;
          }
        }
        remote_tracks.push_back(RemoteTrackEntry{pc, mid, audio});
      } else if (kind == "video" && video) {
        AttachRemoteVideoTrack(pc, mid, video);
      }
    };
  }

  RemoteTrackInfo TrackInfoFor(const std::string& pc,
                               const std::string& mid) const {
    std::lock_guard<std::mutex> lock(remote_mutex);
    const auto it = remote_track_info.find(mid);
    if (it != remote_track_info.end()) return it->second;
    RemoteTrackInfo info;
    info.mid = mid;
    info.kind = "video";
    info.source = "camera";
    if (pc == "p2p") info.uid = "p2p";
    return info;
  }

  void AttachRemoteVideoTrack(
      const std::string& pc, const std::string& mid,
      webrtc::scoped_refptr<webrtc::VideoTrackInterface> track) {
    if (!track) return;
    auto sink = std::make_unique<RemoteVideoSink>(
        mid, [this, pc, mid] { return TrackInfoFor(pc, mid); },
        [this](RemoteVideoFrame frame) {
          MediaSession::RemoteVideoCallback callback;
          {
            std::lock_guard<std::mutex> lock(remote_mutex);
            callback = remote_video_callback;
          }
          if (callback) callback(std::move(frame));
        });
    std::lock_guard<std::mutex> lock(remote_mutex);
    for (auto& entry : remote_video_tracks) {
      if (entry.pc == pc && entry.mid == mid) {
        if (entry.track && entry.sink) {
          entry.track->RemoveSink(entry.sink.get());
        }
        entry.track = track;
        entry.sink = std::move(sink);
        track->AddOrUpdateSink(entry.sink.get(), webrtc::VideoSinkWants());
        track->set_enabled(!sfu_paused.load(std::memory_order_relaxed));
        return;
      }
    }
    track->AddOrUpdateSink(sink.get(), webrtc::VideoSinkWants());
    track->set_enabled(!sfu_paused.load(std::memory_order_relaxed));
    remote_video_tracks.push_back(
        RemoteVideoTrackEntry{pc, mid, track, std::move(sink)});
  }

  void ApplyRemoteTrackState(
      const webrtc::scoped_refptr<webrtc::AudioTrackInterface>& track,
      const std::string& pc) {
    if (!track) return;
    if (auto source = track->GetSource()) {
      source->SetVolume(volume.load(std::memory_order_relaxed));
    }
    const bool paused = deafened.load(std::memory_order_relaxed) ||
                        (pc == "sfu-recv" &&
                         sfu_paused.load(std::memory_order_relaxed));
    track->set_enabled(!paused);
  }

  bool EnsurePlayout(std::string* detail) {
    if (!adm || !worker) {
      if (detail) *detail = "ADM 未初始化";
      return false;
    }
    if (fake_device) {
      // Dummy ADM does not provide a real playout device. Its recording
      // source is enough for negotiation and control-path tests.
      return true;
    }
    const bool ok = worker->BlockingCall([this] {
      if (!adm->PlayoutIsInitialized() && adm->InitPlayout() != 0) {
        return false;
      }
      return adm->Playing() || adm->StartPlayout() == 0;
    });
    if (!ok) {
      if (detail) *detail = "启动音频播放失败";
    } else {
    }
    return ok;
  }

  bool EnsureRecordingStarted(std::string* detail) {
    if (!adm || !worker) {
      if (detail) *detail = "ADM 未初始化";
      return false;
    }
    const bool ok = worker->BlockingCall([this] {
      if (!adm->RecordingIsInitialized() && adm->InitRecording() != 0) {
        return false;
      }
      return adm->Recording() || adm->StartRecording() == 0;
    });
    if (!ok && detail) *detail = "启动麦克风采集失败";
    return ok;
  }

  bool SetMuted(bool next, MediaError* error) {
    if (!initialized) {
      SetError(error, 1720, "音频媒体会话未初始化");
      return false;
    }
    muted.store(next, std::memory_order_relaxed);
    if (capture_gate) capture_gate->SetMuted(next);
    if (audio_track) {
      signaling->BlockingCall(
          [this, next] { audio_track->set_enabled(!next); });
    }
    if (!ApplySendState()) {
      muted.store(true, std::memory_order_relaxed);
      if (capture_gate) capture_gate->SetMuted(true);
      if (audio_track) {
        signaling->BlockingCall([this] { audio_track->set_enabled(false); });
      }
      ApplySendState();
      SetError(error, 1723, "更新音频发送状态失败");
      return false;
    }
    return true;
  }

  bool ApplySendState() {
    const bool send = !muted.load(std::memory_order_relaxed);
    bool ok = true;
    if (sfu_send) {
      ok = sfu_send->SetAudioSenderActive(
               send && !sfu_paused.load(std::memory_order_relaxed)) &&
           ok;
      if (!camera_mid.empty()) {
        ok = sfu_send->SetVideoSenderActive(
                 camera_mid,
                 camera_enabled.load(std::memory_order_relaxed) &&
                     !sfu_paused.load(std::memory_order_relaxed)) &&
             ok;
      }
    }
    if (p2p) ok = p2p->SetAudioSenderActive(send) && ok;
    return ok;
  }

  bool SetCameraEnabled(bool enabled, MediaError* error) {
    if (!initialized || !camera_track) {
      SetError(error, 1730, "视频媒体会话未初始化");
      return false;
    }
    camera_enabled.store(enabled, std::memory_order_relaxed);
    if (!ApplySendState()) {
      camera_enabled.store(false, std::memory_order_relaxed);
      ApplySendState();
      SetError(error, 1731, "更新摄像头发送状态失败");
      return false;
    }
    return true;
  }

  bool PushCameraFrame(const VideoFrameData& frame, MediaError* error) {
    if (!initialized || !camera_source) {
      SetError(error, 1732, "摄像头视频源未初始化");
      return false;
    }
    if (!camera_enabled.load(std::memory_order_relaxed)) return true;
    if (!camera_source->PushFrame(frame)) {
      SetError(error, 1733, "摄像头帧格式无效");
      return false;
    }
    return true;
  }

  bool SetDeafened(bool next, MediaError* error) {
    if (!initialized) {
      SetError(error, 1721, "音频媒体会话未初始化");
      return false;
    }
    deafened.store(next, std::memory_order_relaxed);
    std::vector<RemoteTrackEntry> tracks;
    {
      std::lock_guard<std::mutex> lock(remote_mutex);
      tracks = remote_tracks;
    }
    if (signaling) {
      signaling->BlockingCall([tracks, next] {
        for (const auto& entry : tracks) {
          if (entry.track) entry.track->set_enabled(!next);
        }
      });
    }
    return true;
  }

  bool CreateSfuPublishOffer(MediaSfuOffer* out, MediaError* error) {
    if (!EnsureSfuSend(error)) return false;
    if (!sfu_send->AddAudioTrack(audio_track,
                            webrtc::RtpTransceiverDirection::kSendOnly,
                            "talk", error)) {
      return false;
    }
    if (config.camera_publish_enabled) {
      if (!sfu_send->AddVideoTrack(
              camera_track, webrtc::RtpTransceiverDirection::kSendOnly,
              "talk", error)) {
        return false;
      }
    }
    if (!sfu_send->CreateAndSetLocal(true, error)) return false;
    sfu_send->SetAudioSenderActive(false);
    camera_mid.clear();
    if (config.camera_publish_enabled &&
        sfu_send->GetVideoMid(&camera_mid)) {
      sfu_send->SetVideoSenderActive(camera_mid, false);
    }
    if (out == nullptr) {
      SetError(error, 1722, "offer 输出为空");
      return false;
    }
    if (!sfu_send->GetLocalSdp(&out->sdp, error)) return false;
    if (!sfu_send->GetAudioMid(&out->audio_mid)) out->audio_mid = "0";
    out->camera_mid = camera_mid;
    out->source = config.audio_source == "system" ? "screen" : "mic";
    return true;
  }

  bool ApplySfuPublishAnswer(const std::string& sdp, MediaError* error) {
    if (!sfu_send) {
      SetError(error, 1723, "SFU 发布通道未创建");
      return false;
    }
    if (!sfu_send->SetRemote(webrtc::SdpType::kAnswer, sdp, error)) {
      return false;
    }
    ApplySendState();
    return true;
  }

  bool CreateSfuSubscribeAnswer(const std::string& remote_offer,
                                const std::vector<RemoteTrackInfo>& consumers,
                                std::string* answer,
                                MediaError* error) {
    if (!EnsureSfuRecv(error)) return false;
    {
      std::lock_guard<std::mutex> lock(remote_mutex);
      for (const auto& consumer : consumers) {
        if (consumer.mid.empty()) continue;
        remote_track_info[consumer.mid] = consumer;
      }
    }
    if (!sfu_recv->SetRemote(webrtc::SdpType::kOffer, remote_offer, error)) {
      return false;
    }
    if (!sfu_recv->CreateAndSetLocal(false, error)) return false;
    return sfu_recv->GetLocalSdp(answer, error);
  }

  bool CreateP2pOffer(std::string* offer, MediaError* error) {
    if (!initialized) {
      SetError(error, 1724, "音频媒体会话未初始化");
      return false;
    }
    CloseP2p();
    p2p = std::make_unique<MediaPeer>("p2p", signaling.get(), event,
                                      RemoteCallback());
    if (!p2p->Create(factory.get(), rtc_config, error)) return false;
    std::string playout_error;
    if (!EnsurePlayout(&playout_error)) {
      SetError(error, 1708, playout_error);
      return false;
    }
    if (!p2p->AddAudioTrack(audio_track,
                       webrtc::RtpTransceiverDirection::kSendRecv,
                       "talk-p2p", error)) {
      return false;
    }
    if (!p2p->CreateAndSetLocal(true, error)) return false;
    ApplySendState();
    return p2p->GetLocalSdp(offer, error);
  }

  bool CreateP2pAnswer(const std::string& remote_offer, std::string* answer,
                       MediaError* error) {
    if (!initialized) {
      SetError(error, 1725, "音频媒体会话未初始化");
      return false;
    }
    CloseP2p();
    p2p = std::make_unique<MediaPeer>("p2p", signaling.get(), event,
                                      RemoteCallback());
    if (!p2p->Create(factory.get(), rtc_config, error)) return false;
    std::string playout_error;
    if (!EnsurePlayout(&playout_error)) {
      SetError(error, 1708, playout_error);
      return false;
    }
    if (!p2p->SetRemote(webrtc::SdpType::kOffer, remote_offer, error)) {
      return false;
    }
    if (!p2p->AddAudioTrack(audio_track,
                       webrtc::RtpTransceiverDirection::kSendRecv,
                       "talk-p2p", error)) {
      return false;
    }
    if (!p2p->CreateAndSetLocal(false, error)) return false;
    ApplySendState();
    return p2p->GetLocalSdp(answer, error);
  }

  bool ApplyP2pAnswer(const std::string& answer, MediaError* error) {
    if (!p2p) {
      SetError(error, 1726, "P2P 通道未创建");
      return false;
    }
    if (!p2p->SetRemote(webrtc::SdpType::kAnswer, answer, error)) return false;
    ApplySendState();
    return true;
  }

  void RemoveRemoteTracks(const std::string& pc) {
    std::vector<RemoteVideoTrackEntry> removed_video;
    {
      std::lock_guard<std::mutex> lock(remote_mutex);
      remote_tracks.erase(
          std::remove_if(remote_tracks.begin(), remote_tracks.end(),
                         [&pc](const RemoteTrackEntry& entry) {
                           return entry.pc == pc;
                         }),
          remote_tracks.end());
      for (auto it = remote_video_tracks.begin();
           it != remote_video_tracks.end();) {
        if (it->pc == pc) {
          removed_video.push_back(std::move(*it));
          it = remote_video_tracks.erase(it);
        } else {
          ++it;
        }
      }
    }
    if (signaling) {
      signaling->BlockingCall([removed = std::move(removed_video)]() mutable {
        for (auto& entry : removed) {
          if (entry.track && entry.sink) {
            entry.track->RemoveSink(entry.sink.get());
          }
        }
      });
    }
  }

  void CloseP2p() {
    if (!p2p) return;
    p2p->Close();
    p2p.reset();
    RemoveRemoteTracks("p2p");
  }

  MediaStatus Status() const {
    MediaStatus out;
    out.initialized = initialized;
    out.sfu_send_ready = sfu_send != nullptr;
    out.sfu_recv_ready = sfu_recv != nullptr;
    out.p2p_ready = p2p != nullptr;
    out.p2p_connected = p2p && p2p->state_name() == "connected";
    out.muted = muted.load(std::memory_order_relaxed);
    out.deafened = deafened.load(std::memory_order_relaxed);
    out.sfu_paused = sfu_paused.load(std::memory_order_relaxed);
    out.camera_enabled = camera_enabled.load(std::memory_order_relaxed);
    out.volume = volume.load(std::memory_order_relaxed);
    out.capture_frames =
        std::max(capture_gate ? capture_gate->frames() : 0,
                 capture_monitor ? capture_monitor->frames() : 0);
    if (adm && worker) {
      worker->BlockingCall([this, &out] {
        out.recording_initialized = adm->RecordingIsInitialized();
        out.recording = adm->Recording();
        out.playout_initialized = adm->PlayoutIsInitialized();
        out.playing = adm->Playing();
      });
    }
    out.sfu_send_active =
        sfu_send && sfu_send->HasAudioSender() &&
        sfu_send->AudioSenderActive();
    out.camera_sender_active =
        sfu_send && !camera_mid.empty() &&
        sfu_send->VideoSenderActive(camera_mid);
    out.camera_capture_frames = camera_source ? camera_source->frames() : 0;
    out.p2p_send_active =
        p2p && p2p->HasAudioSender() && p2p->AudioSenderActive();
    PeerMediaStats stats;
    if (p2p && p2p->state_name() == "connected" &&
        p2p->CollectMediaStats(&stats)) {
      out.p2p_audio_packets_sent += stats.audio_packets_sent;
      out.audio_bytes_sent += stats.audio_bytes_sent;
      out.audio_packets_received += stats.audio_packets_received;
      out.audio_bytes_received += stats.audio_bytes_received;
      out.remote_audio_level = std::max(out.remote_audio_level,
                                        stats.audio_level);
    }
    if (sfu_send && sfu_send->state_name() == "connected" &&
        sfu_send->CollectMediaStats(&stats)) {
      out.sfu_audio_packets_sent += stats.audio_packets_sent;
      out.audio_bytes_sent += stats.audio_bytes_sent;
      out.video_packets_sent += stats.video_packets_sent;
      out.video_bytes_sent += stats.video_bytes_sent;
    }
    if (sfu_recv && sfu_recv->state_name() == "connected" &&
        sfu_recv->CollectMediaStats(&stats)) {
      out.audio_packets_received += stats.audio_packets_received;
      out.audio_bytes_received += stats.audio_bytes_received;
      out.video_packets_received += stats.video_packets_received;
      out.video_bytes_received += stats.video_bytes_received;
      out.remote_audio_level = std::max(out.remote_audio_level,
                                        stats.audio_level);
    }
    {
      std::lock_guard<std::mutex> lock(remote_mutex);
      out.remote_tracks = remote_tracks.size();
      for (const auto& entry : remote_tracks) {
        if (entry.track && entry.track->enabled()) ++out.active_remote_tracks;
      }
      out.remote_video_tracks = remote_video_tracks.size();
      for (const auto& entry : remote_video_tracks) {
        if (entry.track && entry.track->enabled()) {
          ++out.active_remote_video_tracks;
        }
      }
    }
    out.recording_device = recording_name;
    out.playout_device = playout_name;
    out.sfu_send_state = sfu_send ? sfu_send->state_name() : "none";
    out.sfu_recv_state = sfu_recv ? sfu_recv->state_name() : "none";
    out.p2p_state = p2p ? p2p->state_name() : "none";
    out.last_error = last_error;
    return out;
  }

  MediaConfig config;
  MediaSession::Event event;
  MediaSession::RemoteVideoCallback remote_video_callback;
  bool initialized = false;
  bool shutdown = false;
  bool fake_device = false;
  std::atomic<bool> muted{true};
  std::atomic<bool> deafened{false};
  std::atomic<bool> sfu_paused{false};
  std::atomic<bool> camera_enabled{false};
  std::atomic<double> volume{0.6};
  std::string last_error;
  std::string recording_name;
  std::string playout_name;

  std::unique_ptr<webrtc::Thread> network;
  std::unique_ptr<webrtc::Thread> worker;
  std::unique_ptr<webrtc::Thread> signaling;
  std::unique_ptr<webrtc::ScopedCOMInitializer> com;
  webrtc::scoped_refptr<webrtc::AudioDeviceModule> adm;
  webrtc::scoped_refptr<SwitchableAudioDevice> switchable;
  webrtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface> factory;
  webrtc::scoped_refptr<webrtc::AudioTrackInterface> audio_track;
  webrtc::scoped_refptr<webrtc::VideoTrackInterface> camera_track;
  webrtc::scoped_refptr<CameraVideoTrackSource> camera_source;
  std::unique_ptr<CaptureMonitor> capture_monitor;
  CaptureGate* capture_gate = nullptr;
  webrtc::PeerConnectionInterface::RTCConfiguration rtc_config;
  std::unique_ptr<MediaPeer> sfu_send;
  std::unique_ptr<MediaPeer> sfu_recv;
  std::unique_ptr<MediaPeer> p2p;
  std::string camera_mid;
  struct RemoteTrackEntry {
    std::string pc;
    std::string mid;
    webrtc::scoped_refptr<webrtc::AudioTrackInterface> track;
  };
  struct RemoteVideoTrackEntry {
    std::string pc;
    std::string mid;
    webrtc::scoped_refptr<webrtc::VideoTrackInterface> track;
    std::unique_ptr<RemoteVideoSink> sink;
  };
  mutable std::mutex remote_mutex;
  std::vector<RemoteTrackEntry> remote_tracks;
  std::vector<RemoteVideoTrackEntry> remote_video_tracks;
  std::map<std::string, RemoteTrackInfo> remote_track_info;
};

MediaSession::MediaSession() : impl_(std::make_unique<Impl>()) {}
MediaSession::~MediaSession() = default;

bool MediaSession::Initialize(const MediaConfig& config, Event event,
                              RemoteVideoCallback remote_video,
                              MediaError* error) {
  return impl_->Initialize(config, std::move(event), std::move(remote_video),
                           error);
}

void MediaSession::Shutdown() { impl_->Shutdown(); }

bool MediaSession::CreateSfuPublishOffer(MediaSfuOffer* offer,
                                              MediaError* error) {
  return impl_->CreateSfuPublishOffer(offer, error);
}

bool MediaSession::ApplySfuPublishAnswer(const std::string& sdp,
                                              MediaError* error) {
  return impl_->ApplySfuPublishAnswer(sdp, error);
}

bool MediaSession::CreateSfuSubscribeAnswer(
    const std::string& remote_offer,
    const std::vector<RemoteTrackInfo>& consumers, std::string* answer,
    MediaError* error) {
  return impl_->CreateSfuSubscribeAnswer(remote_offer, consumers, answer,
                                         error);
}

bool MediaSession::CreateP2pOffer(std::string* offer,
                                       MediaError* error) {
  return impl_->CreateP2pOffer(offer, error);
}

bool MediaSession::CreateP2pAnswer(const std::string& remote_offer,
                                        std::string* answer,
                                        MediaError* error) {
  return impl_->CreateP2pAnswer(remote_offer, answer, error);
}

bool MediaSession::ApplyP2pAnswer(const std::string& answer,
                                       MediaError* error) {
  return impl_->ApplyP2pAnswer(answer, error);
}

bool MediaSession::AddP2pCandidate(const std::string& candidate,
                                        const std::string& mid,
                                        int mline_index,
                                        MediaError* error) {
  if (!impl_->p2p) {
    SetError(error, 1727, "P2P 通道未创建");
    return false;
  }
  return impl_->p2p->AddIceCandidate(candidate, mid, mline_index, error);
}

void MediaSession::CloseP2p() { impl_->CloseP2p(); }

bool MediaSession::SetMuted(bool muted, MediaError* error) {
  return impl_->SetMuted(muted, error);
}

bool MediaSession::SetDeafened(bool deafened, MediaError* error) {
  return impl_->SetDeafened(deafened, error);
}

bool MediaSession::SetVolume(double volume, MediaError* error) {
  if (!std::isfinite(volume) || volume < 0.0 || volume > 1.0) {
    SetError(error, 1728, "音量必须在 0 到 1 之间");
    return false;
  }
  impl_->volume.store(volume, std::memory_order_relaxed);
  std::vector<Impl::RemoteTrackEntry> tracks;
  {
    std::lock_guard<std::mutex> lock(impl_->remote_mutex);
    tracks = impl_->remote_tracks;
  }
  if (impl_->signaling) {
    impl_->signaling->BlockingCall([tracks, volume] {
      for (const auto& entry : tracks) {
        if (entry.track && entry.track->GetSource()) {
          entry.track->GetSource()->SetVolume(volume);
        }
      }
    });
  }
  return true;
}

bool MediaSession::SetSfuPaused(bool paused, MediaError* error) {
  if (!impl_->initialized) {
    SetError(error, 1729, "音频媒体会话未初始化");
    return false;
  }
  impl_->sfu_paused.store(paused, std::memory_order_relaxed);
  impl_->ApplySendState();
  std::vector<Impl::RemoteTrackEntry> tracks;
  std::vector<webrtc::scoped_refptr<webrtc::VideoTrackInterface>>
      video_tracks;
  {
    std::lock_guard<std::mutex> lock(impl_->remote_mutex);
    tracks = impl_->remote_tracks;
    for (const auto& entry : impl_->remote_video_tracks) {
      if (entry.track) video_tracks.push_back(entry.track);
    }
  }
  const bool deafened = impl_->deafened.load(std::memory_order_relaxed);
  if (impl_->signaling) {
    impl_->signaling->BlockingCall([tracks, video_tracks, paused, deafened] {
      for (const auto& entry : tracks) {
        const bool sfu_paused = paused && entry.pc == "sfu-recv";
        if (entry.track) entry.track->set_enabled(!deafened && !sfu_paused);
      }
      for (const auto& track : video_tracks) {
        if (track) track->set_enabled(!deafened && !paused);
      }
    });
  }
  return true;
}

bool MediaSession::SetCameraEnabled(bool enabled, MediaError* error) {
  return impl_->SetCameraEnabled(enabled, error);
}

bool MediaSession::PushCameraFrame(const VideoFrameData& frame,
                                   MediaError* error) {
  return impl_->PushCameraFrame(frame, error);
}

MediaStatus MediaSession::status() const { return impl_->Status(); }

}  // namespace rtcwin::webrtc_media
