// File: core/media/webrtc/webrtc_media.cpp
// Brief: 实现 SFU/P2P 的 SDP、轨道与设备编排。
#include "media/webrtc/webrtc_media.h"

#include <utility>
#include <vector>

#include "api/audio_options.h"
#include "api/make_ref_counted.h"
#include "base/log.h"
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
                         MuteGate gate, Event event, VideoFrameSink video_sink,
                         VideoFrameSink local_video_sink)
    : config_(config),
      event_(std::move(event)),
      video_sink_(std::move(video_sink)),
      local_video_sink_(std::move(local_video_sink)) {
  if (config_.contains("camera_index") && config_["camera_index"].is_number()) {
    camera_index_ = config_["camera_index"].get<int>();
  }
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
  if (camera_source_) {
    camera_source_->Stop();
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
  camera_track_ = nullptr;
  video_track_ = nullptr;
  camera_source_ = nullptr;
  video_source_ = nullptr;
  environment_.reset();
}

Json WebRtcMedia::Offer() {
  if (!environment_ || !SelectDevices(true) || !EnsureAudioTrack() ||
      !EnsureVideoTracks()) {
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
  // join 即发布两路视频 m-line（camera 为第 0 路、screen 为第 1 路）：
  // 服务端会建两个 paused 的 producer，启停只走 setMute / sender active，
  // 不重建 producer（也就不会连带重建音频）。
  if (!AddVideoTrack(*send_, camera_track_,
                     webrtc::RtpTransceiverDirection::kSendOnly, "talk") ||
      !AddVideoTrack(*send_, video_track_,
                     webrtc::RtpTransceiverDirection::kSendOnly, "talk")) {
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
  std::string camera_mid;
  if (!send_->MidFor(TrackKind::kVideo, 0, &camera_mid)) {
    camera_mid = "1";
  }
  std::string screen_mid;
  if (!send_->MidFor(TrackKind::kVideo, 1, &screen_mid)) {
    screen_mid = "2";
  }
  std::string sdp;
  if (!send_->GetLocalSdp(&sdp)) {
    Fail("SDP 序列化失败");
    return Failed();
  }
  return {{"sdp", sdp},
          {"tracks", TrackDescriptions(audio_mid, camera_mid, screen_mid)}};
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
  if (!environment_ || !SelectDevices(true) || !EnsureAudioTrack() ||
      !EnsureVideoTracks()) {
    return Failed();
  }
  // answerer 等收到 offer 时再建 PC，避免刚建就丢掉。
  if (!offerer) {
    return {{"sdp", ""}};
  }
  // 新的分配可能晚于旧会话的 p2p.close，先清掉残留 PC。
  p2p_->Close();
  // OnTrack 在远端 SDP 落地时同步触发，sink 必须在建连前挂好。
  p2p_->SetVideoSink(video_sink_);
  if (!p2p_->Create(environment_->factory(), environment_->rtc_config())) {
    Fail("创建 PeerConnection 失败");
    return Failed();
  }
  environment_->audio()->EnsurePlayout();
  if (!AddAudioTrack(*p2p_, webrtc::RtpTransceiverDirection::kSendRecv,
                     "talk-p2p")) {
    return Failed();
  }
  // 与 SFU 上行同序预发布两路视频：音频/摄像头/屏幕，启停只切 sender。
  if (!AddVideoTrack(*p2p_, camera_track_,
                     webrtc::RtpTransceiverDirection::kSendRecv, "talk-p2p") ||
      !AddVideoTrack(*p2p_, video_track_,
                     webrtc::RtpTransceiverDirection::kSendRecv, "talk-p2p")) {
    return Failed();
  }
  if (!p2p_->CreateAndSetLocal(true)) {
    Fail("创建 P2P offer 失败");
    return Failed();
  }
  ApplyP2pVideoState();
  std::string sdp;
  if (!p2p_->GetLocalSdp(&sdp)) {
    Fail("P2P SDP 序列化失败");
    return Failed();
  }
  return {{"sdp", sdp}};
}

Json WebRtcMedia::AnswerP2p(const std::string& sdp) {
  if (!environment_ || !SelectDevices(true) || !EnsureAudioTrack() ||
      !EnsureVideoTracks()) {
    return Failed();
  }
  p2p_->SetVideoSink(video_sink_);
  if (!p2p_->Create(environment_->factory(), environment_->rtc_config())) {
    Fail("创建 PeerConnection 失败");
    return Failed();
  }
  environment_->audio()->EnsurePlayout();
  // 先应用远端 offer，再挂轨道：应答方用 AddTrack 复用 offer 里的 m-line，
  // 顺序反过来会多出一组 recvonly 轨道，表现为只收不发。
  if (!p2p_->SetRemote(webrtc::SdpType::kOffer, sdp)) {
    Fail("设置 P2P offer 失败");
    return Failed();
  }
  if (!AddAudioTrack(*p2p_, webrtc::RtpTransceiverDirection::kSendRecv,
                     "talk-p2p")) {
    return Failed();
  }
  // m-line 顺序由 offer 决定（音频/摄像头/屏幕），这里按 kind 依次挂轨。
  if (!AddVideoTrack(*p2p_, camera_track_,
                     webrtc::RtpTransceiverDirection::kSendRecv, "talk-p2p") ||
      !AddVideoTrack(*p2p_, video_track_,
                     webrtc::RtpTransceiverDirection::kSendRecv, "talk-p2p")) {
    return Failed();
  }
  if (!p2p_->CreateAndSetLocal(false)) {
    Fail("创建 P2P answer 失败");
    return Failed();
  }
  p2p_->set_gain(gain_.load());
  ApplyP2pVideoState();
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
  // 协商期间用户可能已点共享，这里按当前状态补一次视频 sender。
  ApplyP2pVideoState();
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
  // 切回 SFU（sfu_paused=false）时跟随两路共享状态恢复。
  send_->SetSenderActive(TrackKind::kVideo, 0,
                         camera_sharing_.load() && !paused);
  send_->SetSenderActive(TrackKind::kVideo, 1,
                         video_sharing_.load() && !paused);
}

void WebRtcMedia::ApplyP2pVideoState() {
  if (!p2p_) {
    return;
  }
  // P2P 与 SFU 的 m-line 顺序一致，第 0 路摄像头、第 1 路屏幕。
  std::string camera_mid;
  std::string screen_mid;
  const bool has_camera = p2p_->MidFor(TrackKind::kVideo, 0, &camera_mid);
  const bool has_screen = p2p_->MidFor(TrackKind::kVideo, 1, &screen_mid);
  const bool camera_ok =
      p2p_->SetSenderActive(TrackKind::kVideo, 0, camera_sharing_.load());
  const bool screen_ok =
      p2p_->SetSenderActive(TrackKind::kVideo, 1, video_sharing_.load());
  TALK_LOG_INFO("media",
                "P2P 视频状态 camera_mid={}({}) screen_mid={}({}) 直播中={}/{} "
                "ok={}/{}",
                camera_mid, has_camera ? "有" : "无", screen_mid,
                has_screen ? "有" : "无", camera_sharing_.load(),
                video_sharing_.load(), camera_ok, screen_ok);
}

void WebRtcMedia::SfuPause(bool paused) {
  sfu_paused_ = paused;
  if (recv_) {
    recv_->SetReceivePaused(paused);
  }
  if (p2p_) {
    if (paused) {
      // 进 P2P：直连画面接管，P2P 视频下行恢复。
      p2p_->SetVideoReceivePaused(false);
    } else {
      // 切回 SFU：先停 P2P 视频收发，避免两路 mid 同名画面互相覆盖。
      p2p_->SetSenderActive(TrackKind::kVideo, 0, false);
      p2p_->SetSenderActive(TrackKind::kVideo, 1, false);
      p2p_->SetVideoReceivePaused(true);
    }
  }
  ApplySendState();
}

Json WebRtcMedia::StartVideo(const std::string& source) {
  if (!environment_) {
    return Failed();
  }
  const bool is_camera = source == "camera";
  if (!is_camera && source != "screen") {
    Fail("未知视频源：" + source);
    return Failed();
  }
  std::atomic<bool>& sharing =
      is_camera ? camera_sharing_ : video_sharing_;
  if (sharing.load()) {
    // 重复 start 直接返回当前状态，不重复起采集线程。
    return {{"sharing", true}, {"source", source}};
  }
  if (!EnsureVideoTracks()) {
    return Failed();
  }
  if (is_camera) {
    camera_source_->SetLocalFrameSink(local_video_sink_);
  }
  std::string error;
  const bool started = is_camera ? camera_source_->Start(&error)
                                 : video_source_->Start(&error);
  if (!started) {
    Fail(error.empty() ? (is_camera ? "启动摄像头失败" : "启动桌面采集失败")
                       : error);
    return Failed();
  }
  // P2P 期间 SFU 上行处于暂停，只激活直连 sender；SFU 侧等切回时由
  // ApplySendState 按 sharing 状态恢复。
  const int index = is_camera ? 0 : 1;
  const bool paused = sfu_paused_.load();
  bool ok = send_ ? send_->SetSenderActive(TrackKind::kVideo, index, !paused)
                  : false;
  ok = p2p_->SetSenderActive(TrackKind::kVideo, index, true) && ok;
  TALK_LOG_INFO("media", "视频 sender source={} sfu_paused={} ok={}", source,
                paused, ok);
  if (!ok) {
    if (is_camera) {
      camera_source_->Stop();
    } else {
      video_source_->Stop();
    }
    Fail("设置视频发送状态失败");
    return Failed();
  }
  sharing = true;
  if (is_camera) {
    preview_running_ = true;
  }
  Json out = {{"sharing", true},
              {"source", source},
              {"send_state",
               send_ ? send_->ConnectionStateText() : std::string()}};
  if (is_camera) {
    out["device"] = camera_source_->device_name();
  }
  return out;
}

Json WebRtcMedia::StopVideo(const std::string& source) {
  const bool is_camera = source == "camera";
  if (!is_camera && source != "screen") {
    Fail("未知视频源：" + source);
    return Failed();
  }
  // 先停采集（立即停止出帧），再切 sender；顺序反过来会多发几帧。
  if (is_camera) {
    if (camera_source_) {
      camera_source_->Stop();
    }
    camera_sharing_ = false;
    preview_running_ = false;
  } else {
    if (video_source_) {
      video_source_->Stop();
    }
    video_sharing_ = false;
  }
  if (send_) {
    send_->SetSenderActive(TrackKind::kVideo, is_camera ? 0 : 1, false);
  }
  if (p2p_) {
    p2p_->SetSenderActive(TrackKind::kVideo, is_camera ? 0 : 1, false);
  }
  return {{"sharing", false}, {"source", source}};
}

Json WebRtcMedia::StartCameraPreview(int device_index) {
  if (!environment_) {
    return Failed();
  }
  if (camera_sharing_) {
    return {{"preview", true},
            {"device", camera_source_ ? camera_source_->device_name()
                                      : std::string()},
            {"camera_index", camera_index_}};
  }
  if (preview_running_ && camera_index_ == device_index) {
    return {{"preview", true},
            {"device", camera_source_ ? camera_source_->device_name()
                                      : std::string()},
            {"camera_index", camera_index_}};
  }
  if (preview_running_) {
    preview_running_ = false;
    camera_source_->Stop();
  }
  if (camera_source_ && camera_index_ != device_index) {
    TALK_LOG_INFO("media", "切换摄像头预览设备 old={} new={}", camera_index_,
                  device_index);
    camera_source_->Stop();
    camera_source_->SetLocalFrameSink({});
    camera_source_ = nullptr;
    camera_track_ = nullptr;
  }
  camera_index_ = device_index;
  if (!EnsureVideoTracks()) {
    return Failed();
  }
  camera_source_->SetLocalFrameSink(local_video_sink_);
  std::string error;
  if (!camera_source_->Start(&error)) {
    Fail(error.empty() ? "启动摄像头预览失败" : error);
    return Failed();
  }
  preview_running_ = true;
  return {{"preview", true},
          {"device", camera_source_->device_name()},
          {"camera_index", camera_index_},
          {"width", camera_source_->video_width()},
          {"height", camera_source_->video_height()},
          {"fps", camera_source_->video_fps()}};
}

Json WebRtcMedia::StopCameraPreview() {
  if (!preview_running_) {
    return {{"preview", false}};
  }
  preview_running_ = false;
  // 摄像头已经作为房间视频在发送时，预览开关不能中断正式上行。
  if (!camera_sharing_ && camera_source_) {
    camera_source_->Stop();
  }
  return {{"preview", false}};
}

Json WebRtcMedia::GetCameras() {
  Json cameras = Json::array();
  for (const auto& camera : camera::MFAPICapture::EnumerateDevices()) {
    cameras.push_back({{"index", camera.index}, {"name", camera.name}});
  }
  return {{"cameras", std::move(cameras)}};
}

void WebRtcMedia::SetVideoSink(VideoFrameSink sink) {
  video_sink_ = std::move(sink);
  // 已建好的接收链立即生效；还没建则在 Answer 时下发。
  if (recv_) {
    recv_->SetVideoSink(video_sink_);
  }
  // P2P 的远端视频轨也走同一个 sink，否则直连画面进不了界面。
  if (p2p_) {
    p2p_->SetVideoSink(video_sink_);
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
      {"sharing", video_sharing_.load()},
      {"camera_frames", camera_source_ ? camera_source_->frames() : 0ULL},
      {"camera_sharing", camera_sharing_.load()},
      {"camera_device", camera_source_ ? camera_source_->device_name()
                                        : std::string()}};
  // 上行 sender 实际激活状态与本地静音/暂停标志：无声时先看这里。
  Json send_state = send_ ? send_->SenderActiveState() : Json::object();
  send_state["muted"] = muted_.load();
  send_state["paused"] = sfu_paused_.load();
  out["send_active"] = std::move(send_state);
  // P2P 上行 sender 真实状态：直连画面不动时先看这里。
  if (p2p_) {
    out["p2p_send_active"] = p2p_->SenderActiveState();
  }
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

bool WebRtcMedia::EnsureVideoTracks() {
  if (video_track_ && camera_track_) {
    return true;
  }
  const bool ready = OnSignaling([this] {
    // 只建轨不采集：StartVideo 才起采集线程。
    if (!camera_source_) {
      camera_source_ = webrtc::make_ref_counted<CameraVideoSource>(
          kVideoMaxWidth, kVideoMaxHeight, kVideoFps, camera_index_);
      camera_track_ = environment_->factory()->CreateVideoTrack(camera_source_,
                                                               "camera");
    }
    if (!video_source_) {
      video_source_ = webrtc::make_ref_counted<DesktopVideoSource>(
          kVideoMaxWidth, kVideoMaxHeight, kVideoFps);
      video_track_ = environment_->factory()->CreateVideoTrack(video_source_,
                                                              "screen");
    }
    return camera_track_ != nullptr && video_track_ != nullptr;
  });
  if (!ready) {
    Fail("创建视频轨失败");
  }
  return ready;
}

bool WebRtcMedia::AddVideoTrack(
    PeerConnection& peer,
    webrtc::scoped_refptr<webrtc::VideoTrackInterface> track,
    webrtc::RtpTransceiverDirection direction,
    const std::string& stream_id) {
  if (!track || !peer.AddVideoTrack(track, direction, stream_id)) {
    Fail("添加视频轨失败");
    return false;
  }
  return true;
}

Json WebRtcMedia::TrackDescriptions(const std::string& audio_mid,
                                    const std::string& camera_mid,
                                    const std::string& screen_mid) const {
  const std::string audio_source =
      config_.value("audio_source", std::string("mic"));
  Json tracks = Json::array();
  tracks.push_back({{"mid", audio_mid},
                    {"kind", TrackKindName(TrackKind::kAudio)},
                    {"source", audio_source == "system" ? "screen" : "mic"}});
  // 摄像头路走服务端 media.setMute{source:"camera"} 控制 producer；
  // 屏幕路标 screen，服务端 publish 时直接 resume，启停只切本地 sender。
  tracks.push_back({{"mid", camera_mid},
                    {"kind", TrackKindName(TrackKind::kVideo)},
                    {"source", "camera"}});
  tracks.push_back({{"mid", screen_mid},
                    {"kind", TrackKindName(TrackKind::kVideo)},
                    {"source", "screen"}});
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
                                             VideoFrameSink video_sink,
                                             VideoFrameSink local_video_sink) {
  auto media = std::make_unique<WebRtcMedia>(config, ice_servers,
                                             std::move(gate),
                                             std::move(event),
                                             std::move(video_sink),
                                             std::move(local_video_sink));
  if (!media->ok()) {
    return nullptr;
  }
  return media;
}
