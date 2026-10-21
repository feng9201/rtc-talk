// File: core/media/webrtc/peer_connection.h
// Brief: 单个 PeerConnection 的线程安全包装与事件映射。
#ifndef TALK_MEDIA_WEBRTC_PEER_CONNECTION_H_
#define TALK_MEDIA_WEBRTC_PEER_CONNECTION_H_

#include <atomic>
#include <map>
#include <string>
#include <utility>

#include "api/peer_connection_interface.h"
#include "api/rtp_sender_interface.h"
#include "base/json.h"
#include "media/media.h"
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
  bool CreateAndSetLocal(bool offer);
  bool SetRemote(webrtc::SdpType type, const std::string& sdp);
  bool AddIceCandidate(const std::string& candidate, const std::string& mid,
                       int index);
  bool GetLocalSdp(std::string* sdp);
  bool FirstMid(std::string* mid);
  bool SetSenderActive(bool active);

  void SetReceivePaused(bool paused);
  void set_gain(double gain);

  Json Stats();
  std::string Path();

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
  std::atomic<double> gain_{0.5};
  std::atomic<bool> receive_paused_{false};
  std::string path_;
};

}  // namespace talk

#endif  // TALK_MEDIA_WEBRTC_PEER_CONNECTION_H_
