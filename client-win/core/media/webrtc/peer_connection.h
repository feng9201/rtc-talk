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
  // 按轨道类型取 mid；未发布该类型时返回 false。两路视频需要带 index。
  bool MidFor(TrackKind kind, std::string* mid);
  bool MidFor(TrackKind kind, int index, std::string* mid);
  // 只切换指定类型的 sender；index 区分同类型多路（0 为第一路）。
  bool SetSenderActive(TrackKind kind, bool active);
  bool SetSenderActive(TrackKind kind, int index, bool active);
  // 按 SDP 协商出的 mid 切换，摄像头/屏幕两路互不影响。
  bool SetSenderActiveByMid(const std::string& mid, bool active);
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
      const std::string& stream_id, const std::string& key);
  // 在 signaling 线程内切换 sender；调用方已在 signaling 线程时只走这个。
  bool SetSenderActiveOnSignaling(const std::string& mid, bool active);
  // 挂载一路远端视频轨；按 mid 区分，多路并存（多人会议每人一路）。
  void AttachRemoteVideoOnSignaling(
      webrtc::scoped_refptr<webrtc::VideoTrackInterface> track,
      const std::string& mid);
  void DetachRemoteVideoOnSignaling();
  // 只摘掉指定 mid，重新协商同一路时先清旧 sink。
  void DetachRemoteVideoOnSignaling(const std::string& mid);

  template <class F>
  auto RunOnSignaling(F&& f) {
    return signaling_->BlockingCall(std::forward<F>(f));
  }

  std::string name_;
  Media::Event event_;
  webrtc::Thread* signaling_ = nullptr;
  webrtc::scoped_refptr<webrtc::PeerConnectionInterface> pc_;
  // 上行 sender 以 track id 为键；mid 在 SDP 生成后才可用，激活时按 mid 查。
  std::map<std::string, webrtc::scoped_refptr<webrtc::RtpSenderInterface>>
      senders_;
  std::shared_ptr<VideoSinkHandle> video_sink_handle_;
  // 多路远端视频：每路一个 sink，回调统一带各自 mid；只在 signaling 线程读写。
  struct RemoteVideo {
    webrtc::scoped_refptr<webrtc::VideoTrackInterface> track;
    std::unique_ptr<RemoteVideoSink> sink;
  };
  std::map<std::string, RemoteVideo> remote_videos_;
  std::atomic<double> gain_{0.5};
  std::atomic<bool> receive_paused_{false};
  std::string path_;
};

}  // namespace talk

#endif  // TALK_MEDIA_WEBRTC_PEER_CONNECTION_H_
