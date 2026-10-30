#pragma once
/**
 * @brief:ADM、设备选择、采集、播放、静音、音量
*/
#include "rtcwin/webrtc_media/media_session.h"

#include "api/audio/audio_frame_processor.h"
#include "api/audio_options.h"
#include "api/audio/create_audio_device_module.h"
#include "api/environment/environment.h"
#include "api/media_stream_interface.h"
#include "api/peer_connection_interface.h"
#include "rtc_base/thread.h"
#include "rtc_base/win/scoped_com_initializer.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>

namespace rtcwin::webrtc_media {

class CaptureManager;
class SwitchableAudioDevice;

class AudioPipeline final {
 public:
  AudioPipeline();
  ~AudioPipeline();

  AudioPipeline(const AudioPipeline&) = delete;
  AudioPipeline& operator=(const AudioPipeline&) = delete;

  bool Initialize(webrtc::Thread* worker,
                  const webrtc::Environment& environment,
                  const MediaConfig& config, MediaError* error);
  std::unique_ptr<webrtc::AudioFrameProcessor> CreateFrameProcessor();
  bool CreateTrack(webrtc::Thread* signaling,
                   webrtc::PeerConnectionFactoryInterface* factory,
                   const MediaConfig& config, MediaError* error);
  void Shutdown(webrtc::Thread* worker);

  bool EnsurePlayout(std::string* detail);
  bool SetMuted(bool muted, MediaError* error);
  bool SetDeafened(bool deafened);
  bool SetVolume(double volume, MediaError* error);

  webrtc::scoped_refptr<webrtc::AudioTrackInterface> track() const {
    return audio_track_;
  }
  webrtc::scoped_refptr<webrtc::AudioDeviceModule> adm() const {
    return adm_;
  }
  bool muted() const { return muted_.load(std::memory_order_relaxed); }
  bool deafened() const { return deafened_.load(std::memory_order_relaxed); }
  double volume() const { return volume_.load(std::memory_order_relaxed); }
  bool initialized() const { return initialized_; }
  std::uint64_t capture_frames() const;
  void FillStatus(webrtc::Thread* worker, MediaStatus* status) const;

 private:
  bool InitializeDevices(webrtc::Thread* worker,
                         const webrtc::Environment& environment,
                         const MediaConfig& config, MediaError* error);

  webrtc::Thread* worker_ = nullptr;
  webrtc::Thread* signaling_ = nullptr;
  webrtc::scoped_refptr<webrtc::AudioDeviceModule> adm_;
  webrtc::scoped_refptr<SwitchableAudioDevice> switchable_;
  std::unique_ptr<webrtc::ScopedCOMInitializer> com_;
  webrtc::scoped_refptr<webrtc::AudioTrackInterface> audio_track_;
  std::unique_ptr<CaptureManager> capture_manager_;
  webrtc::AudioFrameProcessor* capture_gate_ = nullptr;
  std::atomic<bool> muted_{true};
  std::atomic<bool> deafened_{false};
  std::atomic<double> volume_{0.6};
  bool fake_device_ = false;
  bool initialized_ = false;
  std::string recording_name_;
  std::string playout_name_;
};

}  // namespace rtcwin::webrtc_media
