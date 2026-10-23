// File: core/media/webrtc/webrtc_media.h
// Brief: libwebrtc 媒体实现，负责 SFU/P2P 的 SDP 与轨道编排。
#ifndef TALK_MEDIA_WEBRTC_WEBRTC_MEDIA_H_
#define TALK_MEDIA_WEBRTC_WEBRTC_MEDIA_H_

#include <atomic>
#include <memory>
#include <string>
#include <utility>

#include "media/media.h"
#include "media/webrtc/media_environment.h"
#include "media/webrtc/peer_connection.h"
#include "media/webrtc/video/camera_video_source.h"
#include "media/webrtc/video/desktop_video_source.h"

namespace talk {

class WebRtcMedia : public Media {
 public:
  WebRtcMedia(const Json& config, const Json& ice_servers, MuteGate gate,
              Event event, VideoFrameSink video_sink);
  ~WebRtcMedia() override;

  WebRtcMedia(const WebRtcMedia&) = delete;
  WebRtcMedia& operator=(const WebRtcMedia&) = delete;

  bool ok() const { return error_.empty(); }

  Json Offer() override;
  Json Accept(const std::string& sdp) override;
  Json Answer(const std::string& sdp) override;

  Json StartP2p(bool offerer) override;
  Json AnswerP2p(const std::string& sdp) override;
  Json AcceptP2p(const std::string& sdp) override;
  bool AddP2pCandidate(const std::string& candidate, const std::string& mid,
                       int index) override;
  void CloseP2p() override;

  void SetMuted(bool muted) override;
  void SetVolume(double gain) override;
  Json SetAudioSource(const std::string& source) override;
  void SfuPause(bool paused) override;

  // 一人两路：摄像头 + 屏幕共享。join 时就发布两条 video m-line，
  // 启停只切采集与对应 sender。
  Json StartVideo(const std::string& source) override;
  Json StopVideo(const std::string& source) override;
  void SetVideoSink(VideoFrameSink sink) override;

  Json GetDevices() override;
  Json GetStats() override;

 private:
  bool SelectDevices(bool need_recording);
  bool EnsureAudioTrack();
  // 两路视频轨（camera / screen）同时建，mid 顺序与 Offer 一致。
  bool EnsureVideoTracks();
  bool AddAudioTrack(PeerConnection& peer,
                     webrtc::RtpTransceiverDirection direction,
                     const std::string& stream_id);
  bool AddVideoTrack(PeerConnection& peer,
                     webrtc::scoped_refptr<webrtc::VideoTrackInterface> track,
                     webrtc::RtpTransceiverDirection direction,
                     const std::string& stream_id);
  // 按当前静音/暂停/共享状态重设 SFU 上行 sender，SDP 落地后必须补一次。
  void ApplySendState();
  Json TrackDescriptions(const std::string& audio_mid,
                         const std::string& camera_mid,
                         const std::string& screen_mid) const;
  void Fail(std::string message);
  Json Failed() const;

  template <class F>
  auto OnSignaling(F&& f) {
    return environment_->RunOnSignaling(std::forward<F>(f));
  }

  Json config_;
  Event event_;
  std::unique_ptr<MediaEnvironment> environment_;
  std::unique_ptr<PeerConnection> send_;
  std::unique_ptr<PeerConnection> recv_;
  std::unique_ptr<PeerConnection> p2p_;
  webrtc::scoped_refptr<webrtc::AudioTrackInterface> audio_track_;
  webrtc::scoped_refptr<CameraVideoSource> camera_source_;
  webrtc::scoped_refptr<DesktopVideoSource> video_source_;
  webrtc::scoped_refptr<webrtc::VideoTrackInterface> camera_track_;
  webrtc::scoped_refptr<webrtc::VideoTrackInterface> video_track_;
  VideoFrameSink video_sink_;
  std::atomic<bool> muted_{true};
  std::atomic<bool> sfu_paused_{false};
  std::atomic<bool> camera_sharing_{false};
  std::atomic<bool> video_sharing_{false};
  std::atomic<double> gain_{0.5};
  int camera_index_ = -1;
  std::string error_;
};

}  // namespace talk

#endif  // TALK_MEDIA_WEBRTC_WEBRTC_MEDIA_H_
