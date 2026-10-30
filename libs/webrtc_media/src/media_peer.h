#pragma once
/**
 * @brief:PeerConnection 生命周期和媒体轨控制
*/
#include "peer_stats_collector.h"
#include "rtcwin/webrtc_media/media_session.h"

#include "api/media_stream_interface.h"
#include "api/peer_connection_interface.h"
#include "rtc_base/thread.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <utility>

namespace rtcwin::webrtc_media {

class MediaPeer final : public webrtc::PeerConnectionObserver {
 public:
  using Event = std::function<void(std::string, std::string)>;
  using RemoteTrack = std::function<void(
      const std::string&, const std::string&, const std::string&,
      webrtc::scoped_refptr<webrtc::AudioTrackInterface>,
      webrtc::scoped_refptr<webrtc::VideoTrackInterface>)>;

  MediaPeer(std::string name, webrtc::Thread* signaling, Event event,
            RemoteTrack remote_track);
  ~MediaPeer() override;

  MediaPeer(const MediaPeer&) = delete;
  MediaPeer& operator=(const MediaPeer&) = delete;

  bool Create(webrtc::PeerConnectionFactoryInterface* factory,
              const webrtc::PeerConnectionInterface::RTCConfiguration& config,
              MediaError* error);
  void Close();

  bool AddAudioTrack(webrtc::scoped_refptr<webrtc::AudioTrackInterface> track,
                     webrtc::RtpTransceiverDirection direction,
                     const std::string& stream_id, MediaError* error);
  bool AddVideoTrack(webrtc::scoped_refptr<webrtc::VideoTrackInterface> track,
                     webrtc::RtpTransceiverDirection direction,
                     const std::string& stream_id, MediaError* error);

  bool CreateAndSetLocal(bool offer, MediaError* error);
  bool SetRemote(webrtc::SdpType type, const std::string& sdp,
                 MediaError* error);
  bool AddIceCandidate(const std::string& candidate, const std::string& mid,
                       int mline_index, MediaError* error);

  bool GetLocalSdp(std::string* sdp, MediaError* error) const;
  bool GetAudioMid(std::string* mid) const;
  bool GetVideoMid(std::string* mid) const;
  bool GetVideoMidForTrack(const std::string& track_id,
                           std::string* mid) const;
  bool HasAudioSender() const;
  bool AudioSenderActive() const;
  bool SetAudioSenderActive(bool active);
  bool HasVideoSender() const;
  bool VideoSenderActive(const std::string& mid) const;
  bool VideoSenderActiveForTrack(const std::string& track_id) const;
  bool SetVideoSenderActive(const std::string& mid, bool active);
  bool SetVideoSenderActiveForTrack(const std::string& track_id,
                                    bool active);
  bool CollectMediaStats(PeerMediaStats* out) const;

  int state() const;
  std::string state_name() const;

 private:
  template <class F>
  auto RunOnSignaling(F&& f) const {
    return signaling_->BlockingCall(std::forward<F>(f));
  }

  void OnSignalingChange(
      webrtc::PeerConnectionInterface::SignalingState) override;
  void OnDataChannel(
      webrtc::scoped_refptr<webrtc::DataChannelInterface>) override;
  void OnIceCandidate(const webrtc::IceCandidate* candidate) override;
  void OnIceGatheringChange(
      webrtc::PeerConnectionInterface::IceGatheringState) override;
  void OnIceSelectedCandidatePairChanged(
      const webrtc::CandidatePairChangeEvent&) override;
  void OnConnectionChange(
      webrtc::PeerConnectionInterface::PeerConnectionState state) override;
  void OnStandardizedIceConnectionChange(
      webrtc::PeerConnectionInterface::IceConnectionState state) override;
  void OnTrack(
      webrtc::scoped_refptr<webrtc::RtpTransceiverInterface> transceiver)
      override;

  void ReportConnectionState(
      webrtc::PeerConnectionInterface::PeerConnectionState state);

  std::string name_;
  webrtc::Thread* signaling_ = nullptr;
  Event event_;
  RemoteTrack remote_track_;
  webrtc::scoped_refptr<webrtc::PeerConnectionInterface> pc_;
  std::map<std::string, webrtc::scoped_refptr<webrtc::RtpSenderInterface>>
      senders_;
  std::atomic<int> state_{0};
  std::atomic<int> reported_state_{0};
};

}  // namespace rtcwin::webrtc_media
