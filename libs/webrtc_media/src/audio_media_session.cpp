#include "rtcwin/webrtc_media/audio_media_session.h"

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
#include "rtc_base/thread.h"
#include "rtc_base/win/scoped_com_initializer.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace rtcwin::webrtc_media {
namespace {

constexpr int kAudioMaxBitrateBps = 32000;
constexpr auto kSdpTimeout = std::chrono::seconds(5);

void SetError(AudioMediaError* error, int code, std::string message) {
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

class AudioPeer final : public webrtc::PeerConnectionObserver {
 public:
  using Event = std::function<void(std::string, std::string)>;
  using RemoteTrack = std::function<void(
      const std::string&, webrtc::scoped_refptr<webrtc::AudioTrackInterface>)>;

  AudioPeer(std::string name, webrtc::Thread* signaling, Event event,
            RemoteTrack remote_track)
      : name_(std::move(name)),
        signaling_(signaling),
        event_(std::move(event)),
        remote_track_(std::move(remote_track)) {}

  ~AudioPeer() override { Close(); }

  bool Create(webrtc::PeerConnectionFactoryInterface* factory,
              const webrtc::PeerConnectionInterface::RTCConfiguration& config,
              AudioMediaError* error) {
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

  bool AddAudio(webrtc::scoped_refptr<webrtc::AudioTrackInterface> track,
                webrtc::RtpTransceiverDirection direction,
                const std::string& stream_id, AudioMediaError* error) {
    if (!track) {
      SetError(error, 1703, "音轨为空");
      return false;
    }
    const bool ok = RunOnSignaling([this, track = std::move(track), direction,
                                    stream_id]() mutable {
      if (!pc_) return false;
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

  bool CreateAndSetLocal(bool offer, AudioMediaError* error) {
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
                 AudioMediaError* error) {
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
                       int mline_index, AudioMediaError* error) {
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

  bool GetLocalSdp(std::string* sdp, AudioMediaError* error) const {
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
    data << "{\"candidate\":\""
         << EscapeJson(candidate != nullptr ? candidate->ToString() : "")
         << "\"";
    if (candidate != nullptr) {
      data << ",\"sdp_mid\":\"" << EscapeJson(candidate->sdp_mid())
           << "\",\"sdp_mline_index\":" << candidate->sdp_mline_index();
    }
    data << "}";
    event_("p2p.candidate", data.str());
  }

  void OnIceGatheringChange(
      webrtc::PeerConnectionInterface::IceGatheringState) override {}

  void OnIceSelectedCandidatePairChanged(
      const webrtc::CandidatePairChangeEvent&) override {}

  void OnConnectionChange(
      webrtc::PeerConnectionInterface::PeerConnectionState state) override {
    state_.store(ConnectionStateCode(state), std::memory_order_relaxed);
    if (!event_) return;
    std::ostringstream data;
    data << "{\"pc\":\"" << EscapeJson(name_) << "\",\"state\":\""
         << ConnectionStateName(state) << "\"}";
    event_("audio.connection", data.str());
  }

  void OnTrack(webrtc::scoped_refptr<webrtc::RtpTransceiverInterface>
                   transceiver) override {
    if (!transceiver || !transceiver->receiver()) return;
    auto track = transceiver->receiver()->track();
    if (!track || track->kind() != "audio") return;
    auto audio = webrtc::scoped_refptr<webrtc::AudioTrackInterface>(
        static_cast<webrtc::AudioTrackInterface*>(track.get()));
    if (remote_track_) remote_track_(name_, audio);
    if (event_) {
      event_("audio.track",
             "{\"pc\":\"" + EscapeJson(name_) + "\",\"mid\":\"" +
                 EscapeJson(transceiver->mid().value_or("")) + "\"}");
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

struct AudioMediaSession::Impl {
  ~Impl() { Shutdown(); }

  bool Initialize(const AudioMediaConfig& next, AudioMediaSession::Event cb,
                  AudioMediaError* error) {
    if (initialized) {
      SetError(error, 1700, "音频媒体会话已初始化");
      return false;
    }
    if (next.audio_backend != "wasapi" && next.audio_backend != "default" &&
        next.audio_backend != "fake") {
      SetError(error, 1701, "不支持的音频后端");
      return false;
    }
    if (next.audio_source != "mic") {
      SetError(error, 1702, "当前版本仅支持麦克风采集");
      return false;
    }
    config = next;
    event = std::move(cb);
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
    network->SetName("rtcwin-audio-network", nullptr);
    worker->SetName("rtcwin-audio-worker", nullptr);
    signaling->SetName("rtcwin-audio-signaling", nullptr);
    if (!network->Start() || !worker->Start() || !signaling->Start()) {
      SetError(error, 1704, "启动 WebRTC 线程失败");
      Shutdown();
      return false;
    }

    const webrtc::Environment environment = webrtc::CreateEnvironment();
    const auto layer = next.audio_backend == "fake"
                           ? webrtc::AudioDeviceModule::kDummyAudio
                           : webrtc::AudioDeviceModule::kWindowsCoreAudio;
    std::string device_error;
    const bool devices_ok =
        worker->BlockingCall([this, &environment, layer, &device_error] {
          if (layer == webrtc::AudioDeviceModule::kWindowsCoreAudio) {
            com = std::make_unique<webrtc::ScopedCOMInitializer>(
                webrtc::ScopedCOMInitializer::kMTA);
            if (!com->Succeeded()) {
              device_error = "COM MTA 初始化失败";
              return false;
            }
          }
          adm = webrtc::CreateAudioDeviceModule(environment, layer);
          if (!adm || adm->Init() != 0) {
            device_error = "音频设备模块初始化失败";
            return false;
          }
          if (layer == webrtc::AudioDeviceModule::kDummyAudio) {
            recording_name = "fake-microphone";
            playout_name = "fake-speaker";
            return true;
          }
          if (!SelectDevice(adm.get(), true, config.recording_device,
                            &recording_name, &device_error)) {
            return false;
          }
          if (!SelectDevice(adm.get(), false, config.playout_device,
                            &playout_name, &device_error)) {
            return false;
          }
          if (adm->InitPlayout() != 0 || adm->StartPlayout() != 0) {
            device_error = "启动音频播放失败";
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
    const bool real_device = next.audio_backend != "fake";
    apm_config.echo_canceller.enabled = real_device;
    apm_config.noise_suppression.enabled = real_device;
    apm_config.gain_controller2.enabled = real_device;
    apm_config.high_pass_filter.enabled = true;
    dependencies.audio_processing_builder =
        std::make_unique<webrtc::BuiltinAudioProcessingBuilder>(apm_config);
    dependencies.audio_encoder_factory =
        webrtc::CreateAudioEncoderFactory<webrtc::AudioEncoderOpus>();
    dependencies.audio_decoder_factory =
        webrtc::CreateAudioDecoderFactory<webrtc::AudioDecoderOpus>();
    dependencies.audio_frame_processor = std::move(gate);
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
      audio_track = factory->CreateAudioTrack("rtcwin-mic", source.get());
      if (!audio_track) return false;
      audio_track->set_enabled(!muted.load(std::memory_order_relaxed));
      return true;
    });
    if (!track_ok) {
      SetError(error, 1707, "创建本地音轨失败");
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
      signaling->BlockingCall([this] { audio_track->set_enabled(false); });
    }
    audio_track = nullptr;
    capture_gate = nullptr;
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
        }
        com.reset();
      });
    } else {
      adm = nullptr;
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
  }

  bool EnsureSfuSend(AudioMediaError* error) {
    if (!initialized) {
      SetError(error, 1710, "音频媒体会话未初始化");
      return false;
    }
    if (!sfu_send) {
      sfu_send = std::make_unique<AudioPeer>("sfu-send", signaling.get(), event,
                                             RemoteCallback());
      if (!sfu_send->Create(factory.get(), rtc_config, error)) return false;
    }
    return true;
  }

  bool EnsureSfuRecv(AudioMediaError* error) {
    if (!initialized) {
      SetError(error, 1711, "音频媒体会话未初始化");
      return false;
    }
    if (!sfu_recv) {
      sfu_recv = std::make_unique<AudioPeer>("sfu-recv", signaling.get(), event,
                                             RemoteCallback());
      if (!sfu_recv->Create(factory.get(), rtc_config, error)) return false;
    }
    return true;
  }

  AudioPeer::RemoteTrack RemoteCallback() {
    return [this](const std::string& pc,
                  webrtc::scoped_refptr<webrtc::AudioTrackInterface> track) {
      if (!track) return;
      ApplyRemoteTrackState(track, pc);
      {
        std::lock_guard<std::mutex> lock(remote_mutex);
        remote_tracks.push_back(RemoteTrackEntry{pc, track});
      }
    };
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

  bool SetMuted(bool next, AudioMediaError* error) {
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
    ApplySendState();
    return true;
  }

  void ApplySendState() {
    const bool send = !muted.load(std::memory_order_relaxed);
    if (sfu_send) {
      sfu_send->SetAudioSenderActive(
          send && !sfu_paused.load(std::memory_order_relaxed));
    }
    if (p2p) p2p->SetAudioSenderActive(send);
  }

  bool SetDeafened(bool next, AudioMediaError* error) {
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

  bool CreateSfuPublishOffer(AudioSfuOffer* out, AudioMediaError* error) {
    if (!EnsureSfuSend(error)) return false;
    if (!sfu_send->AddAudio(audio_track,
                            webrtc::RtpTransceiverDirection::kSendOnly,
                            "talk", error)) {
      return false;
    }
    if (!sfu_send->CreateAndSetLocal(true, error)) return false;
    sfu_send->SetAudioSenderActive(false);
    if (out == nullptr) {
      SetError(error, 1722, "offer 输出为空");
      return false;
    }
    if (!sfu_send->GetLocalSdp(&out->sdp, error)) return false;
    if (!sfu_send->GetAudioMid(&out->audio_mid)) out->audio_mid = "0";
    out->source = config.audio_source;
    return true;
  }

  bool ApplySfuPublishAnswer(const std::string& sdp, AudioMediaError* error) {
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
                                std::string* answer,
                                AudioMediaError* error) {
    if (!EnsureSfuRecv(error)) return false;
    if (!sfu_recv->SetRemote(webrtc::SdpType::kOffer, remote_offer, error)) {
      return false;
    }
    if (!sfu_recv->CreateAndSetLocal(false, error)) return false;
    return sfu_recv->GetLocalSdp(answer, error);
  }

  bool CreateP2pOffer(std::string* offer, AudioMediaError* error) {
    if (!initialized) {
      SetError(error, 1724, "音频媒体会话未初始化");
      return false;
    }
    CloseP2p();
    p2p = std::make_unique<AudioPeer>("p2p", signaling.get(), event,
                                      RemoteCallback());
    if (!p2p->Create(factory.get(), rtc_config, error)) return false;
    if (!p2p->AddAudio(audio_track,
                       webrtc::RtpTransceiverDirection::kSendRecv,
                       "talk-p2p", error)) {
      return false;
    }
    if (!p2p->CreateAndSetLocal(true, error)) return false;
    ApplySendState();
    return p2p->GetLocalSdp(offer, error);
  }

  bool CreateP2pAnswer(const std::string& remote_offer, std::string* answer,
                       AudioMediaError* error) {
    if (!initialized) {
      SetError(error, 1725, "音频媒体会话未初始化");
      return false;
    }
    CloseP2p();
    p2p = std::make_unique<AudioPeer>("p2p", signaling.get(), event,
                                      RemoteCallback());
    if (!p2p->Create(factory.get(), rtc_config, error)) return false;
    if (!p2p->SetRemote(webrtc::SdpType::kOffer, remote_offer, error)) {
      return false;
    }
    if (!p2p->AddAudio(audio_track,
                       webrtc::RtpTransceiverDirection::kSendRecv,
                       "talk-p2p", error)) {
      return false;
    }
    if (!p2p->CreateAndSetLocal(false, error)) return false;
    ApplySendState();
    return p2p->GetLocalSdp(answer, error);
  }

  bool ApplyP2pAnswer(const std::string& answer, AudioMediaError* error) {
    if (!p2p) {
      SetError(error, 1726, "P2P 通道未创建");
      return false;
    }
    if (!p2p->SetRemote(webrtc::SdpType::kAnswer, answer, error)) return false;
    ApplySendState();
    return true;
  }

  void CloseP2p() {
    if (!p2p) return;
    p2p->Close();
    p2p.reset();
  }

  AudioMediaStatus Status() const {
    AudioMediaStatus out;
    out.initialized = initialized;
    out.sfu_send_ready = sfu_send != nullptr;
    out.sfu_recv_ready = sfu_recv != nullptr;
    out.p2p_ready = p2p != nullptr;
    out.p2p_connected = p2p && p2p->state_name() == "connected";
    out.muted = muted.load(std::memory_order_relaxed);
    out.deafened = deafened.load(std::memory_order_relaxed);
    out.sfu_paused = sfu_paused.load(std::memory_order_relaxed);
    out.volume = volume.load(std::memory_order_relaxed);
    out.capture_frames = capture_gate ? capture_gate->frames() : 0;
    {
      std::lock_guard<std::mutex> lock(remote_mutex);
      out.remote_tracks = remote_tracks.size();
    }
    out.recording_device = recording_name;
    out.playout_device = playout_name;
    out.sfu_send_state = sfu_send ? sfu_send->state_name() : "none";
    out.sfu_recv_state = sfu_recv ? sfu_recv->state_name() : "none";
    out.p2p_state = p2p ? p2p->state_name() : "none";
    return out;
  }

  AudioMediaConfig config;
  AudioMediaSession::Event event;
  bool initialized = false;
  bool shutdown = false;
  std::atomic<bool> muted{true};
  std::atomic<bool> deafened{false};
  std::atomic<bool> sfu_paused{false};
  std::atomic<double> volume{0.6};
  std::string recording_name;
  std::string playout_name;

  std::unique_ptr<webrtc::Thread> network;
  std::unique_ptr<webrtc::Thread> worker;
  std::unique_ptr<webrtc::Thread> signaling;
  std::unique_ptr<webrtc::ScopedCOMInitializer> com;
  webrtc::scoped_refptr<webrtc::AudioDeviceModule> adm;
  webrtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface> factory;
  webrtc::scoped_refptr<webrtc::AudioTrackInterface> audio_track;
  CaptureGate* capture_gate = nullptr;
  webrtc::PeerConnectionInterface::RTCConfiguration rtc_config;
  std::unique_ptr<AudioPeer> sfu_send;
  std::unique_ptr<AudioPeer> sfu_recv;
  std::unique_ptr<AudioPeer> p2p;
  struct RemoteTrackEntry {
    std::string pc;
    webrtc::scoped_refptr<webrtc::AudioTrackInterface> track;
  };
  mutable std::mutex remote_mutex;
  std::vector<RemoteTrackEntry> remote_tracks;
};

AudioMediaSession::AudioMediaSession() : impl_(std::make_unique<Impl>()) {}
AudioMediaSession::~AudioMediaSession() = default;

bool AudioMediaSession::Initialize(const AudioMediaConfig& config, Event event,
                                   AudioMediaError* error) {
  return impl_->Initialize(config, std::move(event), error);
}

void AudioMediaSession::Shutdown() { impl_->Shutdown(); }

bool AudioMediaSession::CreateSfuPublishOffer(AudioSfuOffer* offer,
                                              AudioMediaError* error) {
  return impl_->CreateSfuPublishOffer(offer, error);
}

bool AudioMediaSession::ApplySfuPublishAnswer(const std::string& sdp,
                                              AudioMediaError* error) {
  return impl_->ApplySfuPublishAnswer(sdp, error);
}

bool AudioMediaSession::CreateSfuSubscribeAnswer(
    const std::string& remote_offer, std::string* answer,
    AudioMediaError* error) {
  return impl_->CreateSfuSubscribeAnswer(remote_offer, answer, error);
}

bool AudioMediaSession::CreateP2pOffer(std::string* offer,
                                       AudioMediaError* error) {
  return impl_->CreateP2pOffer(offer, error);
}

bool AudioMediaSession::CreateP2pAnswer(const std::string& remote_offer,
                                        std::string* answer,
                                        AudioMediaError* error) {
  return impl_->CreateP2pAnswer(remote_offer, answer, error);
}

bool AudioMediaSession::ApplyP2pAnswer(const std::string& answer,
                                       AudioMediaError* error) {
  return impl_->ApplyP2pAnswer(answer, error);
}

bool AudioMediaSession::AddP2pCandidate(const std::string& candidate,
                                        const std::string& mid,
                                        int mline_index,
                                        AudioMediaError* error) {
  if (!impl_->p2p) {
    SetError(error, 1727, "P2P 通道未创建");
    return false;
  }
  return impl_->p2p->AddIceCandidate(candidate, mid, mline_index, error);
}

void AudioMediaSession::CloseP2p() { impl_->CloseP2p(); }

bool AudioMediaSession::SetMuted(bool muted, AudioMediaError* error) {
  return impl_->SetMuted(muted, error);
}

bool AudioMediaSession::SetDeafened(bool deafened, AudioMediaError* error) {
  return impl_->SetDeafened(deafened, error);
}

bool AudioMediaSession::SetVolume(double volume, AudioMediaError* error) {
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

bool AudioMediaSession::SetSfuPaused(bool paused, AudioMediaError* error) {
  if (!impl_->initialized) {
    SetError(error, 1729, "音频媒体会话未初始化");
    return false;
  }
  impl_->sfu_paused.store(paused, std::memory_order_relaxed);
  impl_->ApplySendState();
  std::vector<Impl::RemoteTrackEntry> tracks;
  {
    std::lock_guard<std::mutex> lock(impl_->remote_mutex);
    tracks = impl_->remote_tracks;
  }
  const bool deafened = impl_->deafened.load(std::memory_order_relaxed);
  if (impl_->signaling) {
    impl_->signaling->BlockingCall([tracks, paused, deafened] {
      for (const auto& entry : tracks) {
        const bool sfu_paused = paused && entry.pc == "sfu-recv";
        if (entry.track) entry.track->set_enabled(!deafened && !sfu_paused);
      }
    });
  }
  return true;
}

AudioMediaStatus AudioMediaSession::status() const { return impl_->Status(); }

}  // namespace rtcwin::webrtc_media



