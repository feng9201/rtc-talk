// File: core/media/webrtc/media_environment.h
// Brief: WebRTC 线程、工厂与音频设备的组合根。
#ifndef TALK_MEDIA_WEBRTC_MEDIA_ENVIRONMENT_H_
#define TALK_MEDIA_WEBRTC_MEDIA_ENVIRONMENT_H_

#include <memory>
#include <string>
#include <utility>

#include "api/audio/audio_processing.h"
#include "api/peer_connection_interface.h"
#include "base/json.h"
#include "media/media.h"
#include "rtc_base/thread.h"

namespace talk {

class AudioDevice;

// 持有 libwebrtc 线程与工厂；PeerConnection 必须先于本对象销毁。
class MediaEnvironment {
 public:
  static std::unique_ptr<MediaEnvironment> Create(const Json& config,
                                                  const Json& ice_servers,
                                                  MuteGate gate,
                                                  std::string* error);
  ~MediaEnvironment();

  MediaEnvironment(const MediaEnvironment&) = delete;
  MediaEnvironment& operator=(const MediaEnvironment&) = delete;

  webrtc::PeerConnectionFactoryInterface* factory() const {
    return factory_.get();
  }
  webrtc::Thread* signaling() const { return signaling_.get(); }
  AudioDevice* audio() const { return audio_.get(); }
  const webrtc::PeerConnectionInterface::RTCConfiguration& rtc_config() const {
    return rtc_config_;
  }

  // 根据采集源动态调整 APM：系统音不需回声/降噪/AGC。
  void ApplyApmForSource(bool system_audio);

  template <class F>
  auto RunOnSignaling(F&& f) {
    return signaling_->BlockingCall(std::forward<F>(f));
  }

 private:
  MediaEnvironment() = default;

  bool Initialize(const Json& config, const Json& ice_servers, MuteGate gate,
                  std::string* error);
  bool ConfigureIceServers(const Json& ice_servers);

  std::unique_ptr<webrtc::Thread> network_;
  std::unique_ptr<webrtc::Thread> worker_;
  std::unique_ptr<webrtc::Thread> signaling_;
  std::unique_ptr<AudioDevice> audio_;
  webrtc::scoped_refptr<webrtc::AudioProcessing> apm_;
  bool real_device_ = false;
  webrtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface> factory_;
  webrtc::PeerConnectionInterface::RTCConfiguration rtc_config_;
};

}  // namespace talk

#endif  // TALK_MEDIA_WEBRTC_MEDIA_ENVIRONMENT_H_
