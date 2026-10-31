#include "rtcwin/webrtc_media/media_session.h"

#include "audio_pipeline.h"
#include "media_peer.h"
#include "remote_track_registry.h"
#include "video/webrtc_h264_factory.h"
#include "video_pipeline.h"

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

void SetError(MediaError* error, int code, std::string message) {
  if (error == nullptr) return;
  error->code = code;
  error->message = std::move(message);
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
    remote_registry = std::make_unique<RemoteTrackRegistry>(signaling.get());
    remote_registry->SetVideoCallback(std::move(remote_video));

    const webrtc::Environment environment = webrtc::CreateEnvironment();
    const bool system_audio = next.audio_source == "system";
    if (!audio_pipeline_.Initialize(worker.get(), environment, next, error)) {
      Shutdown();
      return false;
    }

    webrtc::PeerConnectionFactoryDependencies dependencies;
    dependencies.network_thread = network.get();
    dependencies.worker_thread = worker.get();
    dependencies.signaling_thread = signaling.get();
    dependencies.env = environment;
    dependencies.adm = audio_pipeline_.adm();

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
    dependencies.audio_frame_processor = audio_pipeline_.CreateFrameProcessor();
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

    if (!audio_pipeline_.CreateTrack(signaling.get(), factory.get(), next,
                                     error) ||
        !video_pipeline_.Initialize(signaling.get(), factory.get(), error)) {
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
    audio_pipeline_.Shutdown(worker.get());
    video_pipeline_.Shutdown();
    if (remote_registry) {
      remote_registry->Clear();
      remote_registry.reset();
    }
    factory = nullptr;
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
    if (remote_registry) remote_registry->Clear();
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
      if (!audio_pipeline_.EnsurePlayout(&playout_error)) {
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
      if (!audio_pipeline_.EnsurePlayout(&playout_error)) {
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
        if (remote_registry) {
          remote_registry->AddAudioTrack(pc, mid, std::move(audio));
        }
      } else if (kind == "video" && video) {
        if (remote_registry) {
          remote_registry->AddVideoTrack(pc, mid, std::move(video));
        }
      }
    };
  }

  bool SetMuted(bool next, MediaError* error) {
    if (!initialized) {
      SetError(error, 1720, "音频媒体会话未初始化");
      return false;
    }
    if (!audio_pipeline_.SetMuted(next, error)) return false;
    if (!ApplySendState()) {
      audio_pipeline_.SetMuted(true, nullptr);
      ApplySendState();
      SetError(error, 1723, "更新音频发送状态失败");
      return false;
    }
    return true;
  }

  bool ApplySendState() {
    const bool send = !audio_pipeline_.muted();
    bool ok = true;
    if (sfu_send) {
      ok = sfu_send->SetAudioSenderActive(
               send && !sfu_paused.load(std::memory_order_relaxed)) &&
           ok;
      if (!camera_mid.empty()) {
        ok = sfu_send->SetVideoSenderActive(
                 camera_mid,
                 video_pipeline_.enabled() &&
                     !sfu_paused.load(std::memory_order_relaxed)) &&
             ok;
      }
      if (!screen_mid.empty()) {
        ok = sfu_send->SetVideoSenderActive(
                 screen_mid,
                 video_pipeline_.screen_enabled() &&
                     !sfu_paused.load(std::memory_order_relaxed)) &&
             ok;
      }
    }
    if (p2p) {
      ok = p2p->SetAudioSenderActive(send) && ok;
      if (p2p_video_enabled && video_pipeline_.track()) {
        ok = p2p->SetVideoSenderActiveForTrack(
                 video_pipeline_.track()->id(),
                 video_pipeline_.enabled()) &&
             ok;
      }
      if (p2p_video_enabled && video_pipeline_.screen_track()) {
        ok = p2p->SetVideoSenderActiveForTrack(
                 video_pipeline_.screen_track()->id(),
                 video_pipeline_.screen_enabled()) &&
             ok;
      }
    }
    return ok;
  }

  bool SetCameraEnabled(bool enabled, MediaError* error) {
    if (!initialized || !video_pipeline_.track()) {
      SetError(error, 1730, "视频媒体会话未初始化");
      return false;
    }
    if (!video_pipeline_.SetEnabled(enabled, error)) return false;
    if (!ApplySendState()) {
      video_pipeline_.SetEnabled(false, nullptr);
      ApplySendState();
      SetError(error, 1731, "更新摄像头发送状态失败");
      return false;
    }
    return true;
  }

  bool SetScreenEnabled(bool enabled, MediaError* error) {
    if (!initialized || !video_pipeline_.screen_track()) {
      SetError(error, 1734, "屏幕共享媒体未初始化");
      return false;
    }
    if (!video_pipeline_.SetScreenEnabled(enabled, error)) return false;
    if (!ApplySendState()) {
      video_pipeline_.SetScreenEnabled(false, nullptr);
      ApplySendState();
      SetError(error, 1736, "更新屏幕共享发送状态失败");
      return false;
    }
    return true;
  }

  bool PushCameraFrame(const VideoFrameData& frame, MediaError* error) {
    if (!initialized || !video_pipeline_.track()) {
      SetError(error, 1732, "摄像头视频源未初始化");
      return false;
    }
    return video_pipeline_.PushFrame(frame, error);
  }

  bool SetDeafened(bool next, MediaError* error) {
    if (!initialized) {
      SetError(error, 1721, "音频媒体会话未初始化");
      return false;
    }
    audio_pipeline_.SetDeafened(next);
    if (remote_registry) remote_registry->SetDeafened(next);
    return true;
  }

  bool CreateSfuPublishOffer(MediaSfuOffer* out, MediaError* error,
                             bool ice_restart) {
    if (!EnsureSfuSend(error)) return false;
    if (!sfu_send->AddAudioTrack(audio_pipeline_.track(),
                            webrtc::RtpTransceiverDirection::kSendOnly,
                            "talk", error)) {
      return false;
    }
    if (config.camera_publish_enabled) {
      if (!sfu_send->AddVideoTrack(
              video_pipeline_.track(),
              webrtc::RtpTransceiverDirection::kSendOnly,
              "talk", error)) {
        return false;
      }
    }
    if (config.screen_publish_enabled) {
      if (!sfu_send->AddVideoTrack(
              video_pipeline_.screen_track(),
              webrtc::RtpTransceiverDirection::kSendOnly, "talk", error)) {
        return false;
      }
    }
    if (!sfu_send->CreateAndSetLocal(true, error, ice_restart)) return false;
    sfu_send->SetAudioSenderActive(false);
    camera_mid.clear();
    if (config.camera_publish_enabled &&
        sfu_send->GetVideoMid(&camera_mid)) {
      sfu_send->SetVideoSenderActive(camera_mid, false);
    }
    screen_mid.clear();
    if (config.screen_publish_enabled && video_pipeline_.screen_track() &&
        sfu_send->GetVideoMidForTrack(video_pipeline_.screen_track()->id(),
                                      &screen_mid)) {
      sfu_send->SetVideoSenderActive(screen_mid, false);
    }
    if (out == nullptr) {
      SetError(error, 1722, "offer 输出为空");
      return false;
    }
    if (!sfu_send->GetLocalSdp(&out->sdp, error)) return false;
    if (!sfu_send->GetAudioMid(&out->audio_mid)) out->audio_mid = "0";
    out->camera_mid = camera_mid;
    out->screen_mid = screen_mid;
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
    if (remote_registry) remote_registry->SetTrackInfo(consumers);
    if (!sfu_recv->SetRemote(webrtc::SdpType::kOffer, remote_offer, error)) {
      return false;
    }
    if (!sfu_recv->CreateAndSetLocal(false, error)) return false;
    return sfu_recv->GetLocalSdp(answer, error);
  }

  bool CreateP2pOffer(bool include_video, std::string* offer,
                      MediaError* error) {
    if (!initialized) {
      SetError(error, 1724, "音频媒体会话未初始化");
      return false;
    }
    CloseP2p();
    p2p_video_enabled = include_video;
    p2p = std::make_unique<MediaPeer>("p2p", signaling.get(), event,
                                      RemoteCallback());
    if (!p2p->Create(factory.get(), rtc_config, error)) return false;
    std::string playout_error;
    if (!audio_pipeline_.EnsurePlayout(&playout_error)) {
      SetError(error, 1708, playout_error);
      return false;
    }
    if (!p2p->AddAudioTrack(audio_pipeline_.track(),
                       webrtc::RtpTransceiverDirection::kSendRecv,
                       "talk-p2p", error)) {
      return false;
    }
    if (include_video) {
      if (!p2p->AddVideoTrack(video_pipeline_.track(),
                              webrtc::RtpTransceiverDirection::kSendRecv,
                              "talk-p2p", error) ||
          !p2p->AddVideoTrack(video_pipeline_.screen_track(),
                              webrtc::RtpTransceiverDirection::kSendRecv,
                              "talk-p2p", error)) {
        return false;
      }
    }
    if (!p2p->CreateAndSetLocal(true, error)) return false;
    ApplySendState();
    return p2p->GetLocalSdp(offer, error);
  }

  bool CreateP2pAnswer(const std::string& remote_offer, bool include_video,
                       std::string* answer, MediaError* error) {
    if (!initialized) {
      SetError(error, 1725, "音频媒体会话未初始化");
      return false;
    }
    CloseP2p();
    p2p_video_enabled = include_video;
    p2p = std::make_unique<MediaPeer>("p2p", signaling.get(), event,
                                      RemoteCallback());
    if (!p2p->Create(factory.get(), rtc_config, error)) return false;
    std::string playout_error;
    if (!audio_pipeline_.EnsurePlayout(&playout_error)) {
      SetError(error, 1708, playout_error);
      return false;
    }
    if (!p2p->SetRemote(webrtc::SdpType::kOffer, remote_offer, error)) {
      return false;
    }
    if (!p2p->AddAudioTrack(audio_pipeline_.track(),
                       webrtc::RtpTransceiverDirection::kSendRecv,
                       "talk-p2p", error)) {
      return false;
    }
    if (include_video) {
      if (!p2p->AddVideoTrack(video_pipeline_.track(),
                              webrtc::RtpTransceiverDirection::kSendRecv,
                              "talk-p2p", error) ||
          !p2p->AddVideoTrack(video_pipeline_.screen_track(),
                              webrtc::RtpTransceiverDirection::kSendRecv,
                              "talk-p2p", error)) {
        return false;
      }
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

  bool SetIceServers(const std::vector<IceServer>& ice_servers,
                     MediaError* error) {
    rtc_config.servers.clear();
    for (const auto& item : ice_servers) {
      webrtc::PeerConnectionInterface::IceServer server;
      server.urls = item.urls;
      server.username = item.username;
      server.password = item.password;
      rtc_config.servers.push_back(std::move(server));
    }
    bool ok = true;
    if (sfu_send) ok = sfu_send->SetConfiguration(rtc_config, error) && ok;
    if (sfu_recv) ok = sfu_recv->SetConfiguration(rtc_config, error) && ok;
    if (p2p) ok = p2p->SetConfiguration(rtc_config, error) && ok;
    return ok;
  }

  void CloseP2p() {
    p2p_video_enabled = false;
    if (!p2p) return;
    p2p->Close();
    p2p.reset();
    if (remote_registry) remote_registry->Remove("p2p");
  }

  MediaStatus Status() const {
    MediaStatus out;
    out.initialized = initialized;
    out.sfu_send_ready = sfu_send != nullptr;
    out.sfu_recv_ready = sfu_recv != nullptr;
    out.p2p_ready = p2p != nullptr;
    out.p2p_connected = p2p && p2p->state_name() == "connected";
    out.sfu_paused = sfu_paused.load(std::memory_order_relaxed);
    out.camera_enabled = video_pipeline_.enabled();
    out.screen_enabled = video_pipeline_.screen_enabled();
    audio_pipeline_.FillStatus(worker.get(), &out);
    out.sfu_send_active =
        sfu_send && sfu_send->HasAudioSender() &&
        sfu_send->AudioSenderActive();
    out.camera_sender_active =
        (sfu_send && !camera_mid.empty() &&
         sfu_send->VideoSenderActive(camera_mid)) ||
        (p2p_video_enabled && p2p && video_pipeline_.track() &&
         p2p->VideoSenderActiveForTrack(video_pipeline_.track()->id()));
    out.screen_sender_active =
        (sfu_send && !screen_mid.empty() &&
         sfu_send->VideoSenderActive(screen_mid)) ||
        (p2p_video_enabled && p2p && video_pipeline_.screen_track() &&
         p2p->VideoSenderActiveForTrack(
             video_pipeline_.screen_track()->id()));
    out.camera_capture_frames = video_pipeline_.frames();
    out.screen_capture_frames = video_pipeline_.screen_frames();
    out.p2p_send_active =
        p2p && p2p->HasAudioSender() && p2p->AudioSenderActive();
    PeerMediaStats stats;
    if (p2p && p2p->state_name() == "connected" &&
        p2p->CollectMediaStats(&stats)) {
      out.p2p_audio_packets_sent += stats.audio_packets_sent;
      out.audio_bytes_sent += stats.audio_bytes_sent;
      out.audio_packets_received += stats.audio_packets_received;
      out.audio_bytes_received += stats.audio_bytes_received;
      out.video_packets_sent += stats.video_packets_sent;
      out.video_bytes_sent += stats.video_bytes_sent;
      out.video_packets_received += stats.video_packets_received;
      out.video_bytes_received += stats.video_bytes_received;
      out.packets_lost += stats.packets_lost;
      out.jitter_ms = std::max(out.jitter_ms, stats.jitter_ms);
      out.fraction_lost = std::max(out.fraction_lost, stats.fraction_lost);
      out.rtt_ms = std::max(out.rtt_ms, stats.rtt_ms);
      out.available_outgoing_bitrate_bps =
          std::max(out.available_outgoing_bitrate_bps,
                   stats.available_outgoing_bitrate_bps);
      if (out.candidate_pair.empty()) out.candidate_pair = stats.candidate_pair;
      out.remote_audio_level = std::max(out.remote_audio_level,
                                        stats.audio_level);
    }
    if (sfu_send && sfu_send->state_name() == "connected" &&
        sfu_send->CollectMediaStats(&stats)) {
      out.sfu_audio_packets_sent += stats.audio_packets_sent;
      out.audio_bytes_sent += stats.audio_bytes_sent;
      out.video_packets_sent += stats.video_packets_sent;
      out.video_bytes_sent += stats.video_bytes_sent;
      out.packets_lost += stats.packets_lost;
      out.jitter_ms = std::max(out.jitter_ms, stats.jitter_ms);
      out.fraction_lost = std::max(out.fraction_lost, stats.fraction_lost);
      out.rtt_ms = std::max(out.rtt_ms, stats.rtt_ms);
      out.available_outgoing_bitrate_bps =
          std::max(out.available_outgoing_bitrate_bps,
                   stats.available_outgoing_bitrate_bps);
      if (out.candidate_pair.empty()) out.candidate_pair = stats.candidate_pair;
    }
    if (sfu_recv && sfu_recv->state_name() == "connected" &&
        sfu_recv->CollectMediaStats(&stats)) {
      out.audio_packets_received += stats.audio_packets_received;
      out.audio_bytes_received += stats.audio_bytes_received;
      out.video_packets_received += stats.video_packets_received;
      out.video_bytes_received += stats.video_bytes_received;
      out.packets_lost += stats.packets_lost;
      out.jitter_ms = std::max(out.jitter_ms, stats.jitter_ms);
      out.fraction_lost = std::max(out.fraction_lost, stats.fraction_lost);
      out.rtt_ms = std::max(out.rtt_ms, stats.rtt_ms);
      out.available_outgoing_bitrate_bps =
          std::max(out.available_outgoing_bitrate_bps,
                   stats.available_outgoing_bitrate_bps);
      if (out.candidate_pair.empty()) out.candidate_pair = stats.candidate_pair;
      out.remote_audio_level = std::max(out.remote_audio_level,
                                        stats.audio_level);
    }
    if (remote_registry) {
      const auto counts = remote_registry->counts();
      out.remote_tracks = counts.tracks;
      out.active_remote_tracks = counts.active_tracks;
      out.remote_video_tracks = counts.video_tracks;
      out.active_remote_video_tracks = counts.active_video_tracks;
    }
    out.sfu_send_state = sfu_send ? sfu_send->state_name() : "none";
    out.sfu_recv_state = sfu_recv ? sfu_recv->state_name() : "none";
    out.p2p_state = p2p ? p2p->state_name() : "none";
    out.last_error = last_error;
    return out;
  }

  MediaConfig config;
  MediaSession::Event event;
  bool initialized = false;
  bool shutdown = false;
  std::atomic<bool> sfu_paused{false};
  std::string last_error;

  std::unique_ptr<webrtc::Thread> network;
  std::unique_ptr<webrtc::Thread> worker;
  std::unique_ptr<webrtc::Thread> signaling;
  webrtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface> factory;
  AudioPipeline audio_pipeline_;
  VideoPipeline video_pipeline_;
  webrtc::PeerConnectionInterface::RTCConfiguration rtc_config;
  std::unique_ptr<MediaPeer> sfu_send;
  std::unique_ptr<MediaPeer> sfu_recv;
  std::unique_ptr<MediaPeer> p2p;
  bool p2p_video_enabled = false;
  std::unique_ptr<RemoteTrackRegistry> remote_registry;
  std::string camera_mid;
  std::string screen_mid;
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
                                          MediaError* error,
                                          bool ice_restart) {
  return impl_->CreateSfuPublishOffer(offer, error, ice_restart);
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

bool MediaSession::CreateP2pOffer(bool include_video, std::string* offer,
                                  MediaError* error) {
  return impl_->CreateP2pOffer(include_video, offer, error);
}

bool MediaSession::CreateP2pAnswer(const std::string& remote_offer,
                                   bool include_video, std::string* answer,
                                   MediaError* error) {
  return impl_->CreateP2pAnswer(remote_offer, include_video, answer, error);
}

bool MediaSession::ApplyP2pAnswer(const std::string& answer,
                                       MediaError* error) {
  return impl_->ApplyP2pAnswer(answer, error);
}

void MediaSession::SetP2pPeerUid(const std::string& uid) {
  if (impl_->remote_registry) impl_->remote_registry->SetP2pPeerUid(uid);
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

bool MediaSession::SetIceServers(const std::vector<IceServer>& ice_servers,
                                 MediaError* error) {
  if (!impl_->initialized) {
    SetError(error, 1710, "音频媒体会话未初始化");
    return false;
  }
  return impl_->SetIceServers(ice_servers, error);
}

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
  if (!impl_->audio_pipeline_.SetVolume(volume, error)) return false;
  if (impl_->remote_registry) impl_->remote_registry->SetVolume(volume);
  return true;
}

bool MediaSession::SetRecordingDevice(const std::string& device_id,
                                      MediaError* error) {
  return impl_->audio_pipeline_.SetRecordingDevice(device_id, error);
}

bool MediaSession::SetPlayoutDevice(const std::string& device_id,
                                    MediaError* error) {
  return impl_->audio_pipeline_.SetPlayoutDevice(device_id, error);
}

bool MediaSession::SetSystemAudioDevice(const std::string& device_id,
                                        MediaError* error) {
  return impl_->audio_pipeline_.SetSystemAudioDevice(device_id, error);
}

bool MediaSession::SetSfuPaused(bool paused, MediaError* error) {
  if (!impl_->initialized) {
    SetError(error, 1729, "音频媒体会话未初始化");
    return false;
  }
  impl_->sfu_paused.store(paused, std::memory_order_relaxed);
  impl_->ApplySendState();
  if (impl_->remote_registry) impl_->remote_registry->SetSfuPaused(paused);
  return true;
}

bool MediaSession::SetCameraEnabled(bool enabled, MediaError* error) {
  return impl_->SetCameraEnabled(enabled, error);
}

bool MediaSession::SetScreenEnabled(bool enabled, MediaError* error) {
  return impl_->SetScreenEnabled(enabled, error);
}

bool MediaSession::SetScreenSource(const std::string& source_id, bool window,
                                   MediaError* error) {
  return impl_->video_pipeline_.SetScreenSource(source_id, window, error);
}

bool MediaSession::PushCameraFrame(const VideoFrameData& frame,
                                   MediaError* error) {
  return impl_->PushCameraFrame(frame, error);
}

void MediaSession::SetLocalScreenFrameCallback(
    std::function<void(const VideoFrameData&)> callback) {
  impl_->video_pipeline_.SetScreenFrameCallback(std::move(callback));
}

MediaStatus MediaSession::status() const { return impl_->Status(); }

}  // namespace rtcwin::webrtc_media
