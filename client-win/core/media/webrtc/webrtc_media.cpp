// File: core/media/webrtc/webrtc_media.cpp
// Brief: 实现 SFU/P2P 的 SDP、轨道与设备编排。
#include "media/webrtc/webrtc_media.h"

#include <utility>
#include <vector>

#include "api/audio_options.h"
#include "api/make_ref_counted.h"
#include "media/webrtc/audio_device.h"
#include "media/webrtc/media_stats.h"

namespace talk {
namespace {

// 一期固定采集档位：最长边 1280x720、15fps；码率上限在
// PeerConnection 的 video encoding 里设置。
constexpr int kVideoMaxWidth = 1280;
constexpr int kVideoMaxHeight = 720;
constexpr int kVideoFps = 15;

}  // namespace

WebRtcMedia::WebRtcMedia(const Json& config, const Json& ice_servers,
                         MuteGate gate, Event event, VideoFrameSink video_sink)
    : config_(config),
      event_(std::move(event)),
      video_sink_(std::move(video_sink)) {
  std::string error;
  environment_ = MediaEnvironment::Create(config_, ice_servers,
                                          std::move(gate), &error);
  if (!environment_) {
    Fail(error.empty() ? "媒体初始化失败" : error);
    return;
  }
  send_ = std::make_unique<PeerConnection>("send", event_,
                                           environment_->signaling());
  recv_ = std::make_unique<PeerConnection>("recv", event_,
                                           environment_->signaling());
  p2p_ = std::make_unique<PeerConnection>("p2p", event_,
                                          environment_->signaling());
}

WebRtcMedia::~WebRtcMedia() {
  // 先停采集线程（Stop 内部 join），再关 PeerConnection，
  // 避免采集回调打到已释放的 video track。
  if (video_source_) {
    video_source_->Stop();
  }
  // PeerConnection 必须在创建它的 signaling 线程上关闭。
  if (send_) {
    send_->Close();
  }
  if (recv_) {
    recv_->Close();
  }
  if (p2p_) {
    p2p_->Close();
  }
  send_.reset();
  recv_.reset();
  p2p_.reset();
  audio_track_ = nullptr;
  video_track_ = nullptr;
  video_source_ = nullptr;
  environment_.reset();
}

Json WebRtcMedia::Offer() {
  if (!environment_ || !SelectDevices(true) || !EnsureAudioTrack() ||
      !EnsureVideoTrack()) {
    return Failed();
  }
  if (!send_->Create(environment_->factory(), environment_->rtc_config())) {
    Fail("创建 PeerConnection 失败");
    return Failed();
  }
  environment_->audio()->EnsurePlayout();
  if (!AddAudioTrack(*send_, webrtc::RtpTransceiverDirection::kSendOnly,
                     "talk")) {
    return Failed();
  }
  // join 即发布视频 m-line：服务端会建一个 paused 的视频 producer，
  // 启停共享只走 setMute，不重建 producer（也就不会连带重建音频）。
  if (!AddVideoTrack(*send_, webrtc::RtpTransceiverDirection::kSendOnly,
                     "talk")) {
    return Failed();
  }
  if (!send_->CreateAndSetLocal(true)) {
    Fail("创建或设置本地 SDP 失败");
    return Failed();
  }
  // 新 SFU 上行等服务端应答后再按静音状态恢复；不要动 P2P 上行。
  send_->SetSenderActive(TrackKind::kAudio, false);
  send_->SetSenderActive(TrackKind::kVideo, false);
  std::string audio_mid;
  if (!send_->MidFor(TrackKind::kAudio, &audio_mid)) {
    audio_mid = "0";
  }
  std::string video_mid;
  if (!send_->MidFor(TrackKind::kVideo, &video_mid)) {
    video_mid = "1";
  }
  std::string sdp;
  if (!send_->GetLocalSdp(&sdp)) {
    Fail("SDP 序列化失败");
    return Failed();
  }
  return {{"sdp", sdp},
          {"tracks", TrackDescriptions(audio_mid, video_mid)}};
}

Json WebRtcMedia::Accept(const std::string& sdp) {
  if (!send_ || !send_->SetRemote(webrtc::SdpType::kAnswer, sdp)) {
    Fail("设置远端 SDP 失败");
    return Failed();
  }
  // offer 里两个上行 sender 都先关着；应答落地后必须按当前状态补一次激活，
  // 否则 P2P 切回 SFU（期间不再发 setMute）会一直零上行。
  ApplySendState();
  return Json::object();
}

Json WebRtcMedia::Answer(const std::string& sdp) {
  // 只收不发时不需要采集设备：没有麦克风的机器仍能听别人。
  if (!environment_ || !SelectDevices(false)) {
    return Failed();
  }
  if (!recv_->Create(environment_->factory(), environment_->rtc_config())) {
    Fail("创建 PeerConnection 失败");
    return Failed();
  }
  environment_->audio()->EnsurePlayout();
  // 首个 OnTrack 就会挂 sink，必须在应用远端 offer 之前设置好。
  recv_->SetVideoSink(video_sink_);
  if (!recv_->SetRemote(webrtc::SdpType::kOffer, sdp)) {
    Fail("设置远端 SDP 失败");
    return Failed();
  }
  if (!recv_->CreateAndSetLocal(false)) {
    Fail("创建或设置本地 SDP 失败");
    return Failed();
  }
  recv_->set_gain(gain_.load());
  std::string out;
  if (!recv_->GetLocalSdp(&out)) {
    Fail("SDP 序列化失败");
    return Failed();
  }
  return {{"sdp", out}};
}

Json WebRtcMedia::StartP2p(bool offerer) {
  if (!environment_ || !SelectDevices(true) || !EnsureAudioTrack()) {
    return Failed();
  }
  // answerer 等收到 offer 时再建 PC，避免刚建就丢掉。
  if (!offerer) {
    return {{"sdp", ""}};
  }
  // 新的分配可能晚于旧会话的 p2p.close，先清掉残留 PC。
  p2p_->Close();
  if (!p2p_->Create(environment_->factory(), environment_->rtc_config())) {
    Fail("创建 PeerConnection 失败");
    return Failed();
  }
  environment_->audio()->EnsurePlayout();
  if (!AddAudioTrack(*p2p_, webrtc::RtpTransceiverDirection::kSendRecv,
                     "talk-p2p")) {
    return Failed();
  }
  if (!p2p_->CreateAndSetLocal(true)) {
    Fail("创建 P2P offer 失败");
    return Failed();
  }
  std::string sdp;
  if (!p2p_->GetLocalSdp(&sdp)) {
    Fail("P2P SDP 序列化失败");
    return Failed();
  }
  return {{"sdp", sdp}};
}

Json WebRtcMedia::AnswerP2p(const std::string& sdp) {
  if (!environment_ || !SelectDevices(true) || !EnsureAudioTrack()) {
    return Failed();
  }
  if (!p2p_->Create(environment_->factory(), environment_->rtc_config())) {
    Fail("创建 PeerConnection 失败");
    return Failed();
  }
  environment_->audio()->EnsurePlayout();
  if (!AddAudioTrack(*p2p_, webrtc::RtpTransceiverDirection::kSendRecv,
                     "talk-p2p")) {
    return Failed();
  }
  if (!p2p_->SetRemote(webrtc::SdpType::kOffer, sdp)) {
    Fail("设置 P2P offer 失败");
    return Failed();
  }
  if (!p2p_->CreateAndSetLocal(false)) {
    Fail("创建 P2P answer 失败");
    return Failed();
  }
  p2p_->set_gain(gain_.load());
  std::string out;
  if (!p2p_->GetLocalSdp(&out)) {
    Fail("P2P SDP 序列化失败");
    return Failed();
  }
  return {{"sdp", out}};
}

Json WebRtcMedia::AcceptP2p(const std::string& sdp) {
  if (!p2p_ || !p2p_->SetRemote(webrtc::SdpType::kAnswer, sdp)) {
    Fail("设置 P2P answer 失败");
    return Failed();
  }
  p2p_->set_gain(gain_.load());
  return Json::object();
}

bool WebRtcMedia::AddP2pCandidate(const std::string& candidate,
                                  const std::string& mid, int index) {
  if (!p2p_) {
    return false;
  }
  return p2p_->AddIceCandidate(candidate, mid, index);
}

void WebRtcMedia::CloseP2p() {
  if (p2p_) {
    p2p_->Close();
  }
}

void WebRtcMedia::SetMuted(bool muted) {
  muted_ = muted;
  if (audio_track_) {
    OnSignaling([this, muted] { audio_track_->set_enabled(!muted); });
  }
  bool ok = send_->SetSenderActive(TrackKind::kAudio,
                                   !muted && !sfu_paused_.load());
  ok = p2p_->SetSenderActive(TrackKind::kAudio, !muted) && ok;
  if (!ok && audio_track_) {
    OnSignaling([this] { audio_track_->set_enabled(false); });
    Fail("设置发送状态失败");
  }
}

void WebRtcMedia::SetVolume(double gain) {
  gain_ = gain;
  if (recv_) {
    recv_->set_gain(gain);
  }
  if (p2p_) {
    p2p_->set_gain(gain);
  }
}

Json WebRtcMedia::SetAudioSource(const std::string& source) {
  if (!environment_) {
    return Failed();
  }
  const bool system = source == "system";
  std::string error;
  if (!environment_->audio()->SetAudioSource(system, &error)) {
    Fail(error.empty() ? "切换采集源失败" : error);
    return Failed();
  }
  config_["audio_source"] = source;
  // 采集源变了，同步调整 3A；先切 ADM 再改 APM，避免旧源走新配置。
  environment_->ApplyApmForSource(system);
  return Json::object();
}

void WebRtcMedia::ApplySendState() {
  if (!send_) {
    return;
  }
  const bool paused = sfu_paused_.load();
  send_->SetSenderActive(TrackKind::kAudio, !muted_.load() && !paused);
  // P2P 连通后把 SFU 上行 sender 关掉，避免继续往 SFU 丢包；
  // 切回 SFU（sfu_paused=false）时跟随共享状态恢复。
  send_->SetSenderActive(TrackKind::kVideo, video_sharing_.load() && !paused);
}

void WebRtcMedia::SfuPause(bool paused) {
  sfu_paused_ = paused;
  if (recv_) {
    recv_->SetReceivePaused(paused);
  }
  ApplySendState();
}

Json WebRtcMedia::StartVideo() {
  if (!environment_) {
    return Failed();
  }
  if (video_sharing_.load()) {
    // 重复 start 直接返回当前状态，不重复起采集线程。
    return {{"sharing", true}};
  }
  if (!EnsureVideoTrack()) {
    return Failed();
  }
  std::string error;
  if (!video_source_->Start(&error)) {
    Fail(error.empty() ? "启动桌面采集失败" : error);
    return Failed();
  }
  if (!send_ || !send_->SetSenderActive(TrackKind::kVideo,
                                        !sfu_paused_.load())) {
    video_source_->Stop();
    Fail("设置视频发送状态失败");
    return Failed();
  }
  video_sharing_ = true;
  return {{"sharing", true},
          {"send_state", send_ ? send_->ConnectionStateText() : std::string()}};
}

Json WebRtcMedia::StopVideo() {
  // 先停采集（立即停止出帧），再切 sender；顺序反过来会多发几帧。
  if (video_source_) {
    video_source_->Stop();
  }
  if (send_) {
    send_->SetSenderActive(TrackKind::kVideo, false);
  }
  video_sharing_ = false;
  return {{"sharing", false}};
}

void WebRtcMedia::SetVideoSink(VideoFrameSink sink) {
  video_sink_ = std::move(sink);
  // 已建好的接收链立即生效；还没建则在 Answer 时下发。
  if (recv_) {
    recv_->SetVideoSink(video_sink_);
  }
}

Json WebRtcMedia::GetDevices() {
  if (!environment_) {
    return {{"recording", Json::array()}, {"playout", Json::array()}};
  }
  return environment_->audio()->Enumerate();
}

Json WebRtcMedia::GetStats() {
  if (!environment_) {
    return Json::object();
  }
  Json out =
      MediaStatsCollector::Collect({send_.get(), recv_.get(), p2p_.get()},
                                   environment_->audio());
  // 上行链路状态与采集计数：用于区分“没出帧”“没编码器”“链路未建立”。
  if (send_) {
    out["send_state"] = send_->ConnectionStateText();
  }
  out["video_capture"] = {
      {"frames", video_source_ ? video_source_->frames() : 0ULL},
      {"sharing", video_sharing_.load()}};
  // 上行 sender 实际激活状态与本地静音/暂停标志：无声时先看这里。
  Json send_state = send_ ? send_->SenderActiveState() : Json::object();
  send_state["muted"] = muted_.load();
  send_state["paused"] = sfu_paused_.load();
  out["send_active"] = std::move(send_state);
  return out;
}

bool WebRtcMedia::SelectDevices(bool need_recording) {
  if (!environment_) {
    Fail("媒体环境未初始化");
    return false;
  }
  return environment_->audio()->SelectDevices(need_recording, config_,
                                              &error_);
}

bool WebRtcMedia::EnsureAudioTrack() {
  if (audio_track_) {
    return true;
  }
  const bool ready = OnSignaling([this] {
    auto source =
        environment_->factory()->CreateAudioSource(webrtc::AudioOptions());
    audio_track_ =
        environment_->factory()->CreateAudioTrack("mic", source.get());
    if (!audio_track_) {
      return false;
    }
    audio_track_->set_enabled(!muted_.load());
    return true;
  });
  if (!ready) {
    Fail("创建音轨失败");
  }
  return ready;
}

bool WebRtcMedia::AddAudioTrack(PeerConnection& peer,
                                webrtc::RtpTransceiverDirection direction,
                                const std::string& stream_id) {
  if (!peer.AddAudioTrack(audio_track_, direction, stream_id)) {
    Fail("添加音轨失败");
    return false;
  }
  return true;
}

bool WebRtcMedia::EnsureVideoTrack() {
  if (video_track_) {
    return true;
  }
  const bool ready = OnSignaling([this] {
    // 只建轨不采集：StartVideo 才起采集线程。
    video_source_ = webrtc::make_ref_counted<DesktopVideoSource>(
        kVideoMaxWidth, kVideoMaxHeight, kVideoFps);
    video_track_ = environment_->factory()->CreateVideoTrack(video_source_,
                                                             "screen");
    return video_track_ != nullptr;
  });
  if (!ready) {
    Fail("创建视频轨失败");
  }
  return ready;
}

bool WebRtcMedia::AddVideoTrack(PeerConnection& peer,
                                webrtc::RtpTransceiverDirection direction,
                                const std::string& stream_id) {
  if (!video_track_ ||
      !peer.AddVideoTrack(video_track_, direction, stream_id)) {
    Fail("添加视频轨失败");
    return false;
  }
  return true;
}

Json WebRtcMedia::TrackDescriptions(const std::string& audio_mid,
                                    const std::string& video_mid) const {
  const std::string audio_source =
      config_.value("audio_source", std::string("mic"));
  Json tracks = Json::array();
  tracks.push_back({{"mid", audio_mid},
                    {"kind", TrackKindName(TrackKind::kAudio)},
                    {"source", audio_source == "system" ? "screen" : "mic"}});
  // 服务端的 media.setMute{source:"camera"} 只作用于 source != "screen"
  // 的视频 producer，所以屏幕共享也必须标 camera，否则无法启停。
  tracks.push_back({{"mid", video_mid},
                    {"kind", TrackKindName(TrackKind::kVideo)},
                    {"source", "camera"}});
  return tracks;
}

void WebRtcMedia::Fail(std::string message) { error_ = std::move(message); }

Json WebRtcMedia::Failed() const {
  return {{"error", error_.empty() ? std::string("媒体操作失败") : error_}};
}

}  // namespace talk

std::unique_ptr<talk::Media> talk::MakeMedia(const Json& config,
                                             const Json& ice_servers,
                                             MuteGate gate,
                                             Media::Event event,
                                             VideoFrameSink video_sink) {
  auto media = std::make_unique<WebRtcMedia>(config, ice_servers,
                                             std::move(gate),
                                             std::move(event),
                                             std::move(video_sink));
  if (!media->ok()) {
    return nullptr;
  }
  return media;
}
