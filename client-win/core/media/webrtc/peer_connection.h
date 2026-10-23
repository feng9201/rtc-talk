// File: core/media/webrtc/peer_connection.h
// Brief: 单个 PeerConnection 的线程安全包装与事件映射。
#ifndef TALK_MEDIA_WEBRTC_PEER_CONNECTION_H_
#define TALK_MEDIA_WEBRTC_PEER_CONNECTION_H_

#include <atomic>
#include <map>
#include <memory>
#include <string>
#include <utility>

#include "api/media_stream_interface.h"
#include "api/peer_connection_interface.h"
#include "api/rtp_sender_interface.h"
#include "api/video/video_source_interface.h"
#include "base/json.h"
#include "media/media.h"
#include "media/webrtc/video/video_frame_sink.h"
#include "rtc_base/thread.h"

namespace talk {

// 所有 PC API 都通过 signaling 线程执行；send/recv/p2p 共用该包装。
class PeerConnection : public webrtc::PeerConnectionObserver {
 public:
  PeerConnection(std::string name, Media::Event event,
                 webrtc::Thread* signaling);
  ~PeerConnection() override;

  PeerConnection(const PeerConnection&) = delete;
  PeerConnection& operator=(const PeerConnection&) = delete;

  bool Create(webrtc::PeerConnectionFactoryInterface* factory,
              const webrtc::PeerConnectionInterface::RTCConfiguration& config);
  void Close();

  bool AddAudioTrack(
      webrtc::scoped_refptr<webrtc::AudioTrackInterface> track,
      webrtc::RtpTransceiverDirection direction,
      const std::string& stream_id);
  bool AddVideoTrack(
      webrtc::scoped_refptr<webrtc::VideoTrackInterface> track,
      webrtc::RtpTransceiverDirection direction,
      const std::string& stream_id);
  bool CreateAndSetLocal(bool offer);
  bool SetRemote(webrtc::SdpType type, const std::string& sdp);
  bool AddIceCandidate(const std::string& candidate, const std::string& mid,
                       int index);
  bool GetLocalSdp(std::string* sdp);
  // 按轨道类型取 mid；未发布该类型时返回 false。
  bool MidFor(TrackKind kind, std::string* mid);
  // 只切换指定类型的 sender，避免静音音频时连带停掉视频。
  bool SetSenderActive(TrackKind kind, bool active);
  // 上行 sender 最近一次生效的激活状态；统计日志用它区分“没发”与“不让发”。
  Json SenderActiveState();
  // 下行视频帧回调；可随时替换，空 sink 表示停止回调。
  void SetVideoSink(VideoFrameSink sink);

  void SetReceivePaused(bool paused);
  void set_gain(double gain);

  Json Stats();
  std::string Path();
  // PC 连接状态文本；用于在日志里区分“没发”与“链路未建立”。
  std::string ConnectionStateText();

  const std::string& name() const { return name_; }

 private:
  void OnSignalingChange(
      webrtc::PeerConnectionInterface::SignalingState) override;
  void OnDataChannel(
      webrtc::scoped_refptr<webrtc::DataChannelInterface>) override;
  void OnIceCandidate(const webrtc::IceCandidate* candidate) override;
  void OnIceGatheringChange(
      webrtc::PeerConnectionInterface::IceGatheringState) override;
  void OnIceSelectedCandidatePairChanged(
      const webrtc::CandidatePairChangeEvent& event) override;
  void OnConnectionChange(
      webrtc::PeerConnectionInterface::PeerConnectionState state) override;
  void OnTrack(
      webrtc::scoped_refptr<webrtc::RtpTransceiverInterface> transceiver)
      override;

  void ApplyGainOnSignaling(double gain);
  void ApplyReceiveStateOnSignaling(bool paused);
  bool AddTrack(
      TrackKind kind,
      webrtc::scoped_refptr<webrtc::MediaStreamTrackInterface> track,
      webrtc::RtpTransceiverDirection direction,
      const std::string& stream_id);
  // 挂载第一路远端视频轨；其余 mid 只记一次日志。
  void AttachRemoteVideoOnSignaling(
      webrtc::scoped_refptr<webrtc::VideoTrackInterface> track,
      const std::string& mid);
  void DetachRemoteVideoOnSignaling();

  template <class F>
  auto RunOnSignaling(F&& f) {
    return signaling_->BlockingCall(std::forward<F>(f));
  }

  std::string name_;
  Media::Event event_;
  webrtc::Thread* signaling_ = nullptr;
  webrtc::scoped_refptr<webrtc::PeerConnectionInterface> pc_;
  std::map<TrackKind, webrtc::scoped_refptr<webrtc::RtpSenderInterface>>
      senders_;
  // 与 senders_ 同步，只在 signaling 线程读写。
  std::map<TrackKind, bool> sender_active_;
  std::shared_ptr<VideoSinkHandle> video_sink_handle_;
  std::unique_ptr<RemoteVideoSink> remote_video_sink_;
  webrtc::scoped_refptr<webrtc::VideoTrackInterface> remote_video_track_;
  std::string remote_video_mid_;
  std::atomic<double> gain_{0.5};
  std::atomic<bool> receive_paused_{false};
  std::string path_;
};

}  // namespace talk

#endif  // TALK_MEDIA_WEBRTC_PEER_CONNECTION_H_
