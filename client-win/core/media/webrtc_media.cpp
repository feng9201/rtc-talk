#include "media.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <future>
#include <mutex>
#include <thread>
#include <vector>

#include "api/audio/audio_device.h"
#include "api/audio/audio_frame.h"
#include "api/audio/audio_frame_processor.h"
#include "api/audio/audio_processing.h"
#include "api/audio/builtin_audio_processing_builder.h"
#include "api/audio/create_audio_device_module.h"
#include "api/audio_codecs/audio_decoder_factory_template.h"
#include "api/audio_codecs/audio_encoder_factory_template.h"
#include "api/audio_codecs/opus/audio_decoder_opus.h"
#include "api/audio_codecs/opus/audio_encoder_opus.h"
#include "api/audio_options.h"
#include "api/candidate.h"
#include "api/create_modular_peer_connection_factory.h"
#include "api/enable_media.h"
#include "api/environment/environment_factory.h"
#include "api/jsep.h"
#include "api/make_ref_counted.h"
#include "api/peer_connection_interface.h"
#include "api/rtp_transceiver_interface.h"
#include "api/stats/rtc_stats_collector_callback.h"
#include "api/stats/rtc_stats_report.h"
#include "api/stats/rtcstats_objects.h"
#include "modules/audio_device/include/audio_device_default.h"
#include "p2p/base/port.h"
#include "rtc_base/logging.h"
#include "rtc_base/thread.h"
#include "rtc_base/win/scoped_com_initializer.h"

namespace talk {
namespace {
using namespace std::chrono_literals;

// 编码前执行安全静音，不等待 SDP/设备操作队列。
class CaptureGate : public webrtc::AudioFrameProcessor {
 public:
  CaptureGate(MuteGate gate, std::atomic<uint64_t>* frames) : gate_(std::move(gate)), frames_(frames) {}
  void Process(std::unique_ptr<webrtc::AudioFrame> frame) override {
    if (gate_->load()) frame->Mute();
    if (frames_) frames_->fetch_add(1, std::memory_order_relaxed);
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

 private:
  MuteGate gate_;
  std::atomic<uint64_t>* frames_;
  std::mutex mutex_;
  OnAudioFrameCallback sink_;
};

// 合成音频后端，用于无麦克风环境验证 SFU 链路。
class SineDevice : public webrtc::webrtc_impl::AudioDeviceModuleDefault<webrtc::AudioDeviceModule> {
 public:
  ~SineDevice() override { Terminate(); }
  int32_t RegisterAudioCallback(webrtc::AudioTransport* t) override {
    transport_ = t;
    return 0;
  }
  int32_t StartRecording() override {
    recording_ = true;
    start();
    return 0;
  }
  int32_t StartPlayout() override {
    playing_ = true;
    start();
    return 0;
  }
  int32_t StopRecording() override {
    recording_ = false;
    return 0;
  }
  int32_t StopPlayout() override {
    playing_ = false;
    return 0;
  }
  bool Recording() const override { return recording_; }
  bool Playing() const override { return playing_; }
  int32_t Terminate() override {
    running_ = false;
    if (thread_.joinable()) thread_.join();
    return 0;
  }
  Json stats() const {
    return {{"frames", frames_.load()},       {"signal_frames", signal_frames_.load()},
            {"rms", rms_.load()},             {"clipped", clipped_.load()},
            {"frequency_hz", frequency_.load()}};
  }

 private:
  void start() {
    if (thread_.joinable()) return;
    running_ = true;
    thread_ = std::thread([this] { run(); });
  }
  void run() {
    uint64_t sample = 0;
    auto next = std::chrono::steady_clock::now();
    int crossings = 0, measured = 0;
    int16_t previous = 0;
    while (running_) {
      next += 10ms;
      int16_t buffer[480]{};
      auto* t = transport_.load();
      if (t && recording_) {
        for (auto& s : buffer) {
          s = static_cast<int16_t>(3000 * std::sin(6.283185307179586 * 440 * double(sample++) / 48000));
        }
        uint32_t level = 0;
        t->RecordedDataIsAvailable(buffer, 480, 2, 1, 48000, 0, 0, 0, false, level);
      }
      if (t && playing_) {
        size_t out = 0;
        int64_t elapsed = 0, ntp = 0;
        t->NeedMorePlayData(480, 2, 1, 48000, buffer, out, &elapsed, &ntp);
        double energy = 0;
        out = std::min(out, size_t(480));
        for (size_t i = 0; i < out; ++i) {
          energy += double(buffer[i]) * buffer[i];
          if (buffer[i] == INT16_MAX || buffer[i] == INT16_MIN) ++clipped_;
          if (previous <= 0 && buffer[i] > 0) ++crossings;
          previous = buffer[i];
        }
        rms_ = out ? int(std::sqrt(energy / out)) : 0;
        if (rms_ > 100) ++signal_frames_;
        ++frames_;
        measured += int(out);
        if (measured >= 48000) {
          frequency_ = double(crossings) * 48000 / measured;
          crossings = 0;
          measured = 0;
        }
      }
      std::this_thread::sleep_until(next);
    }
  }

  std::atomic<webrtc::AudioTransport*> transport_{nullptr};
  std::atomic<bool> running_{false}, playing_{false}, recording_{false};
  std::atomic<int> frames_{0}, signal_frames_{0}, rms_{0}, clipped_{0};
  std::atomic<double> frequency_{0};
  std::thread thread_;
};

template <class T>
T wait(std::future<T> future) {
  if (future.wait_for(8s) != std::future_status::ready) return T{};
  return future.get();
}

class CreateObserver : public webrtc::CreateSessionDescriptionObserver {
 public:
  std::promise<std::unique_ptr<webrtc::SessionDescriptionInterface>> promise;
  void OnSuccess(webrtc::SessionDescriptionInterface* d) override {
    promise.set_value(std::unique_ptr<webrtc::SessionDescriptionInterface>(d));
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
  void OnStatsDelivered(const webrtc::scoped_refptr<const webrtc::RTCStatsReport>& r) override {
    promise.set_value(r);
  }
};

class Peer : public webrtc::PeerConnectionObserver {
 public:
  Peer(std::string name, Media::Event event) : name_(std::move(name)), event_(std::move(event)) {}
  void OnSignalingChange(webrtc::PeerConnectionInterface::SignalingState) override {}
  void OnDataChannel(webrtc::scoped_refptr<webrtc::DataChannelInterface>) override {}
  void OnIceCandidate(const webrtc::IceCandidate* candidate) override {
    if (!event_ || name_ != "p2p") return;
    Json data = {{"candidate", candidate ? candidate->ToString() : std::string()}};
    if (candidate) {
      data["sdp_mid"] = candidate->sdp_mid();
      data["sdp_mline_index"] = candidate->sdp_mline_index();
    }
    event_("p2p.candidate", std::move(data));
  }
  void OnIceGatheringChange(webrtc::PeerConnectionInterface::IceGatheringState) override {}
  void OnIceSelectedCandidatePairChanged(const webrtc::CandidatePairChangeEvent& e) override {
    using webrtc::IceCandidateType;
    const auto local = e.selected_candidate_pair.local_candidate().type();
    const auto remote = e.selected_candidate_pair.remote_candidate().type();
    path = (local == IceCandidateType::kRelay || remote == IceCandidateType::kRelay) ? "relay"
         : (local == IceCandidateType::kSrflx || remote == IceCandidateType::kSrflx) ? "srflx"
         : "host";
  }
  void OnConnectionChange(webrtc::PeerConnectionInterface::PeerConnectionState state) override {
    if (!event_) return;
    event_("media.state", {{"pc", name_}, {"state", static_cast<int>(state)}});
    if (name_ != "p2p") return;
    const char* text = state == webrtc::PeerConnectionInterface::PeerConnectionState::kConnected  ? "connected"
                     : state == webrtc::PeerConnectionInterface::PeerConnectionState::kConnecting ? "connecting"
                     : state == webrtc::PeerConnectionInterface::PeerConnectionState::kFailed     ? "failed"
                     : state == webrtc::PeerConnectionInterface::PeerConnectionState::kClosed     ? "closed"
                                                                                                  : nullptr;
    if (!text) return;
    Json data = {{"state", text}};
    if (state == webrtc::PeerConnectionInterface::PeerConnectionState::kConnected) {
      data["path"] = path.empty() ? "host" : path;
    }
    event_("p2p.state", std::move(data));
  }
  void OnTrack(webrtc::scoped_refptr<webrtc::RtpTransceiverInterface> t) override {
    auto track = t->receiver()->track();
    if (track && track->kind() == "audio") {
      auto* audio = static_cast<webrtc::AudioTrackInterface*>(track.get());
      if (audio->GetSource()) audio->GetSource()->SetVolume(gain.load());
      // P2P 期间 SFU 下行只暂停播放，保持 ICE/DTLS 不断，切回时可直接复用。
      track->set_enabled(!paused.load());
    }
  }
  // 只能在 signaling 线程调用。
  void reset() {
    pc = nullptr;
    sender = nullptr;
    path.clear();
  }
  std::atomic<double> gain{0.5};
  std::atomic<bool> paused{false};
  std::string path;
  webrtc::scoped_refptr<webrtc::PeerConnectionInterface> pc;
  webrtc::scoped_refptr<webrtc::RtpSenderInterface> sender;

 private:
  std::string name_;
  Media::Event event_;
};

class WebRtcMedia : public Media {
 public:
  WebRtcMedia(const Json& config, const Json& ice, MuteGate gate, Event event)
      : config_(config), event_(std::move(event)), send_("send", event_), recv_("recv", event_),
        p2p_("p2p", event_) {
    // 默认不输出 WebRTC 的 SDP/候选日志，避免凭证进入诊断输出。
    const bool verbose = config_.value("webrtc_log", false);
    webrtc::LogMessage::LogToDebug(verbose ? webrtc::LS_INFO : webrtc::LS_NONE);
    if (verbose) {
      webrtc::LogMessage::LogTimestamps();
      webrtc::LogMessage::LogThreads();
    }
    if (!init(gate, ice)) {
      if (error_.empty()) error_ = "媒体初始化失败";
    }
  }

  ~WebRtcMedia() override {
    // PeerConnection 必须在创建它的 signaling 线程上关闭。
    if (signaling_ && signaling_started_) {
      signaling_->BlockingCall([this] {
        if (send_.pc) send_.pc->Close();
        if (recv_.pc) recv_.pc->Close();
        if (p2p_.pc) p2p_.pc->Close();
        send_.reset();
        recv_.reset();
        p2p_.reset();
        track_ = nullptr;
        factory_ = nullptr;
      });
    } else {
      send_.reset();
      recv_.reset();
      p2p_.reset();
    }
    if (worker_ && worker_started_) {
      worker_->BlockingCall([this] {
        if (adm_) {
          adm_->StopRecording();
          adm_->StopPlayout();
          adm_->Terminate();
        }
        adm_ = nullptr;
        sine_ = nullptr;
        com_.reset();
      });
    }
    network_.reset();
    signaling_.reset();
    worker_.reset();
  }

  bool ok() const { return error_.empty(); }

  Json offer() override {
    if (!select_devices() || !create(send_) || !ensure_track() ||
        !add_track(send_, webrtc::RtpTransceiverDirection::kSendOnly, "talk")) {
      return Json::object();
    }
    if (!set_local(send_, true)) {
      fail("创建或设置本地 SDP 失败");
      return Json::object();
    }
    // 新 SFU 上行等服务端应答后再按静音状态恢复；不要动 P2P 上行。
    on_signaling([this] { set_send_active(send_, false); });
    std::string mid = on_signaling([this] {
      const auto transceivers = send_.pc->GetTransceivers();
      if (!transceivers.empty() && transceivers.front()->mid().has_value()) {
        return *transceivers.front()->mid();
      }
      return std::string("0");
    });
    std::string sdp;
    if (!local(send_, sdp)) {
      fail("SDP 序列化失败");
      return Json::object();
    }
    return {{"sdp", sdp},
            {"tracks", Json::array({{{"mid", mid}, {"kind", "audio"}, {"source", "mic"}}})}};
  }

  void accept(const std::string& sdp) override {
    if (!set_remote(send_, webrtc::SdpType::kAnswer, sdp)) fail("设置远端 SDP 失败");
  }

  Json answer(const std::string& sdp) override {
    if (!select_devices() || !create(recv_)) return Json::object();
    if (!set_remote(recv_, webrtc::SdpType::kOffer, sdp)) {
      fail("设置远端 SDP 失败");
      return Json::object();
    }
    if (!set_local(recv_, false)) {
      fail("创建或设置本地 SDP 失败");
      return Json::object();
    }
    volume(recv_.gain.load());
    std::string out;
    if (!local(recv_, out)) {
      fail("SDP 序列化失败");
      return Json::object();
    }
    return {{"sdp", out}};
  }

  // ---- S3：1v1 P2P ----
  Json p2p_start(bool offerer) override {
    if (!select_devices() || !ensure_track()) return Json::object();
    // answerer 等收到 offer 时再建 PC，避免刚建就丢掉。
    if (!offerer) return {{"sdp", ""}};
    // 新的分配可能晚于旧会话的 p2p.close，先清掉残留 PC。
    on_signaling([this] {
      if (p2p_.pc) p2p_.pc->Close();
      p2p_.reset();
    });
    if (!create(p2p_) || !add_track(p2p_, webrtc::RtpTransceiverDirection::kSendRecv, "talk-p2p")) {
      return Json::object();
    }
    std::string sdp;
    if (!set_local(p2p_, true) || !local(p2p_, sdp)) {
      fail("创建 P2P offer 失败");
      return Json::object();
    }
    return {{"sdp", sdp}};
  }

  Json p2p_answer(const std::string& sdp) override {
    if (!select_devices() || !create(p2p_) || !ensure_track() ||
        !add_track(p2p_, webrtc::RtpTransceiverDirection::kSendRecv, "talk-p2p")) {
      return Json::object();
    }
    if (!set_remote(p2p_, webrtc::SdpType::kOffer, sdp)) {
      fail("设置 P2P offer 失败");
      return Json::object();
    }
    if (!set_local(p2p_, false)) {
      fail("创建 P2P answer 失败");
      return Json::object();
    }
    volume(p2p_.gain.load());
    std::string out;
    if (!local(p2p_, out)) {
      fail("P2P SDP 序列化失败");
      return Json::object();
    }
    return {{"sdp", out}};
  }

  void p2p_accept(const std::string& sdp) override {
    if (!set_remote(p2p_, webrtc::SdpType::kAnswer, sdp)) {
      fail("设置 P2P answer 失败");
      return;
    }
    volume(p2p_.gain.load());
  }

  bool p2p_candidate(const std::string& candidate, const std::string& mid, int index) override {
    if (!p2p_.pc) return false;
    if (candidate.empty()) return true;  // 结束标记，无需添加
    auto ice = webrtc::IceCandidate::Create(mid, index, candidate);
    if (!ice) {
      fail("P2P 候选解析失败");
      return false;
    }
    return on_signaling([this, &ice] { return p2p_.pc->AddIceCandidate(ice.get()); });
  }

  void p2p_close() override {
    if (!p2p_.pc) return;
    on_signaling([this] {
      if (p2p_.pc) p2p_.pc->Close();
      p2p_.reset();
    });
  }

  void sfu_pause(bool paused) override {
    sfu_paused_ = paused;
    on_signaling([this, paused] {
      recv_.paused = paused;
      if (recv_.pc) {
        for (auto& receiver : recv_.pc->GetReceivers()) {
          auto track = receiver->track();
          if (track) track->set_enabled(!paused);
        }
      }
      set_send_active(send_, !muted_.load() && !paused);
    });
  }

  void mute(bool muted) override {
    muted_ = muted;
    const bool ok = on_signaling([this, muted] {
      if (track_) track_->set_enabled(!muted);
      bool ok = true;
      for (auto* sender : {&send_.sender, &p2p_.sender}) {
        if (!*sender) continue;
        auto p = (*sender)->GetParameters();
        for (auto& e : p.encodings) {
          e.active = !muted;
          e.max_bitrate_bps = 32000;
        }
        if (!(*sender)->SetParameters(p).ok()) ok = false;
      }
      if (!ok && track_) track_->set_enabled(false);
      // P2P 期间暂停 SFU 上行；解除暂停后仍按当前静音状态恢复。
      set_send_active(send_, !muted && !sfu_paused_.load());
      return ok;
    });
    if (!ok) fail("设置发送状态失败");
  }

  Json devices() override {
    Json out = {{"recording", Json::array()}, {"playout", Json::array()}};
    if (!worker_ || !worker_started_ || !adm_) return out;
    return worker_->BlockingCall([this] {
      Json result = {{"recording", Json::array()}, {"playout", Json::array()}};
      for (bool recording : {true, false}) {
        const int count = recording ? adm_->RecordingDevices() : adm_->PlayoutDevices();
        for (int i = 0; i < count; ++i) {
          char name[webrtc::kAdmMaxDeviceNameSize]{}, guid[webrtc::kAdmMaxGuidSize]{};
          const int status = recording ? adm_->RecordingDeviceName(i, name, guid)
                                       : adm_->PlayoutDeviceName(i, name, guid);
          if (status == 0) {
            result[recording ? "recording" : "playout"].push_back(
                {{"index", i}, {"name", name}, {"id", *guid ? guid : name}});
          }
        }
      }
      return result;
    });
  }

  void volume(double gain) override {
    recv_.gain = gain;
    p2p_.gain = gain;
    on_signaling([this, gain] {
      for (auto* peer : {&recv_, &p2p_}) {
        if (!peer->pc) continue;
        for (auto& receiver : peer->pc->GetReceivers()) {
          auto track = receiver->track();
          if (track && track->kind() == "audio") {
            auto* audio = static_cast<webrtc::AudioTrackInterface*>(track.get());
            if (audio->GetSource()) audio->GetSource()->SetVolume(gain);
          }
        }
      }
    });
  }

  Json stats() override {
    struct Item {
      std::string name;
      webrtc::scoped_refptr<StatsObserver> observer;
      std::future<webrtc::scoped_refptr<const webrtc::RTCStatsReport>> future;
    };
    std::vector<Item> items;
    std::string p2p_path;
    // GetStats 及其回调都在 signaling 线程，必须先在那边发起，再在媒体线程等结果。
    on_signaling([this, &items, &p2p_path] {
      for (auto* peer : {&send_, &recv_, &p2p_}) {
        if (!peer->pc) continue;
        auto observer = webrtc::make_ref_counted<StatsObserver>();
        auto future = observer->promise.get_future();
        peer->pc->GetStats(observer.get());
        items.push_back({peer == &send_ ? "send" : peer == &recv_ ? "recv" : "p2p", observer,
                         std::move(future)});
      }
      p2p_path = p2p_.path;
    });
    Json out = Json::object();
    for (auto& item : items) {
      auto report = wait(std::move(item.future));
      if (!report.get()) continue;
      uint64_t received = 0, sent = 0;
      int64_t lost = 0;
      double jitter = 0;
      for (const auto* s : report->GetStatsOfType<webrtc::RTCInboundRtpStreamStats>()) {
        if (s->kind.value_or("") == "audio") {
          received += s->packets_received.value_or(0);
          lost += s->packets_lost.value_or(0);
          jitter = std::max(jitter, s->jitter.value_or(0));
        }
      }
      for (const auto* s : report->GetStatsOfType<webrtc::RTCOutboundRtpStreamStats>()) {
        if (s->kind.value_or("") == "audio") sent += s->packets_sent.value_or(0);
      }
      out[item.name] = {
          {"received", received}, {"sent", sent}, {"lost", lost}, {"jitter_seconds", jitter}};
    }
    if (!p2p_path.empty()) out["p2p_path"] = p2p_path;
    if (sine_) out["fake_playout"] = sine_->stats();
    out["capture"] = {{"frames", capture_frames_.load()}};
    if (adm_ && !sine_ && worker_ && worker_started_) {
      out["device"] = worker_->BlockingCall([this] {
        bool stereo_recording = false, stereo_playout = false;
        uint16_t delay_ms = 0;
        adm_->StereoRecording(&stereo_recording);
        adm_->StereoPlayout(&stereo_playout);
        adm_->PlayoutDelay(&delay_ms);
        return Json{{"recording", adm_->Recording()},
                    {"playing", adm_->Playing()},
                    {"recording_initialized", adm_->RecordingIsInitialized()},
                    {"playout_initialized", adm_->PlayoutIsInitialized()},
                    {"stereo_recording", stereo_recording},
                    {"stereo_playout", stereo_playout},
                    {"playout_delay_ms", delay_ms},
                    {"playout_error", playout_error_},
                    {"recording_device", recording_name_},
                    {"playout_device", playout_name_}};
      });
    }
    return out;
  }

 private:
  bool init(MuteGate gate, const Json& ice) {
    std::string backend = "none";
    if (config_.contains("audio_backend")) {
      if (!config_["audio_backend"].is_string()) {
        fail("audio_backend 必须是字符串");
        return false;
      }
      backend = config_["audio_backend"].get<std::string>();
    }
    if (backend != "fake" && backend != "wasapi" && backend != "default") {
      fail("未启用音频后端");
      return false;
    }

    network_ = webrtc::Thread::CreateWithSocketServer();
    worker_ = webrtc::Thread::Create();
    signaling_ = webrtc::Thread::Create();
    if (!network_ || !worker_ || !signaling_) {
      fail("线程创建失败");
      return false;
    }
    network_->SetName("talk-network", nullptr);
    worker_->SetName("talk-worker", nullptr);
    signaling_->SetName("talk-signaling", nullptr);
    if (!network_->Start() || !worker_->Start() || !signaling_->Start()) {
      fail("线程启动失败");
      return false;
    }
    worker_started_ = true;
    signaling_started_ = true;

    auto env = webrtc::CreateEnvironment();
    if (backend == "fake") {
      sine_ = webrtc::make_ref_counted<SineDevice>();
      adm_ = sine_;
    } else {
      // Core Audio ADM 必须在 COM 线程创建；worker 线程固定为 MTA。
      const bool ready = worker_->BlockingCall([this, &env] {
        com_ = std::make_unique<webrtc::ScopedCOMInitializer>(
            webrtc::ScopedCOMInitializer::kMTA);
        if (!com_->Succeeded()) return false;
        adm_ = webrtc::CreateAudioDeviceModule(
            env, webrtc::AudioDeviceModule::kWindowsCoreAudio);
        if (!adm_) return false;
        return adm_->Init() == 0;
      });
      if (!ready) {
        fail("Windows Core Audio 设备初始化失败");
        return false;
      }
    }

    webrtc::PeerConnectionFactoryDependencies deps;
    deps.network_thread = network_.get();
    deps.worker_thread = worker_.get();
    deps.signaling_thread = signaling_.get();
    deps.env = env;
    deps.adm = adm_;

    webrtc::AudioProcessing::Config apm;
    const bool real_device = backend != "fake";
    apm.echo_canceller.enabled = real_device;
    apm.noise_suppression.enabled = real_device;
    apm.gain_controller2.enabled = real_device;
    apm.high_pass_filter.enabled = true;
    if (real_device) {
      deps.audio_processing_builder =
          std::make_unique<webrtc::BuiltinAudioProcessingBuilder>(apm);
    }
    deps.audio_encoder_factory = webrtc::CreateAudioEncoderFactory<webrtc::AudioEncoderOpus>();
    deps.audio_decoder_factory = webrtc::CreateAudioDecoderFactory<webrtc::AudioDecoderOpus>();
    deps.audio_frame_processor = std::make_unique<CaptureGate>(std::move(gate), &capture_frames_);

    webrtc::EnableMedia(deps);
    factory_ = webrtc::CreateModularPeerConnectionFactory(std::move(deps));
    if (!factory_) {
      fail("PeerConnectionFactory 创建失败");
      return false;
    }

    rtc_config_.sdp_semantics = webrtc::SdpSemantics::kUnifiedPlan;
    if (ice.is_array()) {
      for (const auto& item : ice) {
        if (!item.is_object() || !item.contains("urls")) continue;
        webrtc::PeerConnectionInterface::IceServer server;
        const auto& urls = item["urls"];
        if (urls.is_array()) {
          for (const auto& u : urls) {
            if (u.is_string()) server.urls.push_back(u.get<std::string>());
          }
        } else if (urls.is_string()) {
          server.urls.push_back(urls.get<std::string>());
        }
        if (server.urls.empty()) continue;
        if (item.contains("username") && item["username"].is_string()) {
          server.username = item["username"].get<std::string>();
        }
        if (item.contains("credential") && item["credential"].is_string()) {
          server.password = item["credential"].get<std::string>();
        }
        rtc_config_.servers.push_back(std::move(server));
      }
    }
    return true;
  }

  bool ensure_track() {
    if (track_) return true;
    const bool ready = on_signaling([this] {
      auto source = factory_->CreateAudioSource(webrtc::AudioOptions());
      track_ = factory_->CreateAudioTrack("mic", source.get());
      if (!track_) return false;
      track_->set_enabled(!muted_.load());
      return true;
    });
    if (!ready) fail("创建音轨失败");
    return ready;
  }

  bool add_track(Peer& peer, webrtc::RtpTransceiverDirection direction, const char* stream) {
    if (peer.sender) return true;
    const bool ready = on_signaling([this, &peer, direction, stream] {
      webrtc::RtpTransceiverInit init;
      init.direction = direction;
      init.stream_ids = {stream};
      auto r = peer.pc->AddTransceiver(track_, init);
      if (!r.ok()) return false;
      peer.sender = r.value()->sender();
      return true;
    });
    if (!ready) fail("添加音轨失败");
    return ready;
  }

  // 只能在 signaling 线程调用。
  void set_send_active(Peer& peer, bool active) {
    if (!peer.sender) return;
    auto p = peer.sender->GetParameters();
    for (auto& e : p.encodings) {
      e.active = active;
      e.max_bitrate_bps = 32000;
    }
    peer.sender->SetParameters(p);
  }

  bool create(Peer& peer) {
    if (peer.pc) return true;
    const bool created = on_signaling([this, &peer] {
      auto r = factory_->CreatePeerConnectionOrError(
          rtc_config_, webrtc::PeerConnectionDependencies(&peer));
      if (!r.ok()) return false;
      peer.pc = r.MoveValue();
      return true;
    });
    if (!created) {
      fail("创建 PeerConnection 失败");
      return false;
    }
    ensure_playout();
    return true;
  }

  bool select_devices() {
    if (selected_) return true;
    if (sine_) {
      selected_ = true;
      return true;
    }
    const auto list = devices();
    for (bool recording : {true, false}) {
      const auto key = recording ? "recording_device" : "playout_device";
      std::string wanted;
      if (config_.contains(key)) {
        if (!config_[key].is_string()) {
          fail("音频设备配置必须是字符串");
          return false;
        }
        wanted = config_[key].get<std::string>();
      }
      int index = -1;
      if (!wanted.empty()) {
        for (const auto& d : list[recording ? "recording" : "playout"]) {
          if (d.is_object() && d.contains("id") && d["id"].is_string() &&
              d["id"].get<std::string>() == wanted) {
            index = d.value("index", -1);
            break;
          }
        }
      }
      int status = -1;
      if (index >= 0) {
        status = worker_->BlockingCall([this, recording, index] {
          const auto slot = static_cast<uint16_t>(index);
          const int32_t r = recording ? adm_->SetRecordingDevice(slot)
                                      : adm_->SetPlayoutDevice(slot);
          if (r == 0) {
            char name[webrtc::kAdmMaxDeviceNameSize]{};
            char guid[webrtc::kAdmMaxGuidSize]{};
            if (recording) {
              adm_->RecordingDeviceName(slot, name, guid);
            } else {
              adm_->PlayoutDeviceName(slot, name, guid);
            }
            (recording ? recording_name_ : playout_name_) = name;
          }
          return static_cast<int>(r);
        });
      }
      if (status != 0) {
        status = worker_->BlockingCall([this, recording] {
          if (recording) {
            if (adm_->SetRecordingDevice(
                    webrtc::AudioDeviceModule::kDefaultCommunicationDevice) != 0) {
              return -1;
            }
            recording_name_ = "default-communication";
          } else {
            if (adm_->SetPlayoutDevice(
                    webrtc::AudioDeviceModule::kDefaultCommunicationDevice) != 0) {
              return -1;
            }
            playout_name_ = "default-communication";
          }
          return 0;
        });
      }
      if (status != 0) {
        fail("音频设备不可用");
        return false;
      }
    }
    selected_ = true;
    return true;
  }

  // 必须在 AudioTransport 注册之后再启动 playout：Core Audio 在媒体已启动时
  // 会拒绝 RegisterAudioCallback（"Failed to set audio transport since media was active"）。
  // 同时内置 AEC 要求 playout 先于 recording 启动。
  bool ensure_playout() {
    if (sine_ || playout_started_) return true;
    playout_started_ = worker_->BlockingCall([this] {
      if (adm_->Playing()) return true;
      if (!adm_->PlayoutIsInitialized() && adm_->InitPlayout() != 0) return false;
      return adm_->StartPlayout() == 0;
    });
    if (!playout_started_) playout_error_ = true;
    return playout_started_;
  }

  bool set_local(Peer& p, bool offer) {
    if (!p.pc) return false;
    auto c = webrtc::make_ref_counted<CreateObserver>();
    auto f = c->promise.get_future();
    on_signaling([&] {
      webrtc::PeerConnectionInterface::RTCOfferAnswerOptions options;
      if (offer) {
        p.pc->CreateOffer(c.get(), options);
      } else {
        p.pc->CreateAnswer(c.get(), options);
      }
    });
    auto description = wait(std::move(f));
    if (!description) return false;
    auto s = webrtc::make_ref_counted<SetObserver>();
    auto sf = s->promise.get_future();
    auto* raw = description.release();
    on_signaling([&] { p.pc->SetLocalDescription(s.get(), raw); });
    return wait(std::move(sf));
  }

  bool set_remote(Peer& p, webrtc::SdpType type, const std::string& sdp) {
    if (!p.pc) return false;
    auto d = webrtc::CreateSessionDescription(type, sdp);
    if (!d) return false;
    auto s = webrtc::make_ref_counted<SetObserver>();
    auto f = s->promise.get_future();
    auto* raw = d.release();
    on_signaling([&] { p.pc->SetRemoteDescription(s.get(), raw); });
    return wait(std::move(f));
  }

  bool local(Peer& p, std::string& out) {
    if (!p.pc) return false;
    return on_signaling([&] {
      if (!p.pc->local_description()) return false;
      return p.pc->local_description()->ToString(&out);
    });
  }

  void fail(const char* message) {
    if (error_.empty()) error_ = message;
  }

  // PeerConnection/factory 的 API 必须在创建它们的 signaling 线程上调用。
  template <class F>
  auto on_signaling(F&& f) {
    return signaling_->BlockingCall(std::forward<F>(f));
  }

  Json config_;
  Event event_;
  std::atomic<uint64_t> capture_frames_{0};
  std::string recording_name_, playout_name_;
  bool playout_error_ = false;
  bool playout_started_ = false;
  std::unique_ptr<webrtc::Thread> network_, worker_, signaling_;
  webrtc::scoped_refptr<webrtc::AudioDeviceModule> adm_;
  webrtc::scoped_refptr<SineDevice> sine_;
  std::unique_ptr<webrtc::ScopedCOMInitializer> com_;
  webrtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface> factory_;
  webrtc::PeerConnectionInterface::RTCConfiguration rtc_config_;
  Peer send_, recv_, p2p_;
  std::atomic<bool> muted_{true};
  std::atomic<bool> sfu_paused_{false};
  webrtc::scoped_refptr<webrtc::AudioTrackInterface> track_;
  bool selected_ = false;
  bool worker_started_ = false;
  bool signaling_started_ = false;
  std::string error_;
};
}  // namespace

std::unique_ptr<Media> make_media(const Json& config, const Json& ice, MuteGate gate,
                                  Media::Event event) {
  auto media = std::make_unique<WebRtcMedia>(config, ice, std::move(gate), std::move(event));
  if (!media->ok()) return nullptr;
  return media;
}
}  // namespace talk
