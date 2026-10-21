// File: core/media/webrtc/switchable_audio_device.h
// Brief: 麦克风与系统音 loopback 之间切换的 AudioDeviceModule 聚合器。
#ifndef TALK_MEDIA_WEBRTC_SWITCHABLE_AUDIO_DEVICE_H_
#define TALK_MEDIA_WEBRTC_SWITCHABLE_AUDIO_DEVICE_H_

#include <atomic>
#include <string>

#include "api/audio/audio_device.h"
#include "base/json.h"
#include "media/webrtc/loopback_audio_capture.h"
#include "modules/audio_device/include/audio_device_default.h"

namespace talk {

// 录音源由 source 决定：麦克风交给内置 Core Audio ADM，系统音交给
// WASAPI loopback。播放、设备枚举、音量与统计仍由内置 ADM 负责，因此
// PeerConnection 与 AudioTrack 不需要感知采集源的差异。
class SwitchableAudioDevice
    : public webrtc::webrtc_impl::AudioDeviceModuleDefault<
          webrtc::AudioDeviceModule> {
 public:
  SwitchableAudioDevice(webrtc::scoped_refptr<webrtc::AudioDeviceModule> mic,
                        bool system_audio);
  ~SwitchableAudioDevice() override;

  int32_t RegisterAudioCallback(webrtc::AudioTransport* transport) override;
  int32_t Init() override;
  int32_t Terminate() override;
  bool Initialized() const override;
  int32_t ActiveAudioLayer(AudioLayer* audio_layer) const override;

  // 录音：系统音模式走 loopback，麦克风模式转发内置 ADM。
  int32_t InitRecording() override;
  int32_t StartRecording() override;
  int32_t StopRecording() override;
  bool RecordingIsInitialized() const override;
  bool Recording() const override;
  int32_t SetRecordingDevice(uint16_t index) override;
  int32_t SetRecordingDevice(WindowsDeviceType device) override;
  int32_t SetStereoRecording(bool enable) override;
  int32_t StereoRecording(bool* enabled) const override;
  int32_t StereoRecordingIsAvailable(bool* available) const override;
  int16_t RecordingDevices() override;
  int32_t RecordingDeviceName(uint16_t index,
                              char name[webrtc::kAdmMaxDeviceNameSize],
                              char guid[webrtc::kAdmMaxGuidSize]) override;
  int32_t RecordingIsAvailable(bool* available) override;
  int32_t InitMicrophone() override;
  bool MicrophoneIsInitialized() const override;

  // 播放、设备枚举与音量全部转发内置 ADM。
  int16_t PlayoutDevices() override;
  int32_t PlayoutDeviceName(uint16_t index,
                            char name[webrtc::kAdmMaxDeviceNameSize],
                            char guid[webrtc::kAdmMaxGuidSize]) override;
  int32_t SetPlayoutDevice(uint16_t index) override;
  int32_t SetPlayoutDevice(WindowsDeviceType device) override;
  int32_t PlayoutIsAvailable(bool* available) override;
  int32_t InitPlayout() override;
  bool PlayoutIsInitialized() const override;
  int32_t StartPlayout() override;
  int32_t StopPlayout() override;
  bool Playing() const override;
  int32_t InitSpeaker() override;
  bool SpeakerIsInitialized() const override;
  int32_t SpeakerVolumeIsAvailable(bool* available) override;
  int32_t SetSpeakerVolume(uint32_t volume) override;
  int32_t SpeakerVolume(uint32_t* volume) const override;
  int32_t MaxSpeakerVolume(uint32_t* max_volume) const override;
  int32_t MinSpeakerVolume(uint32_t* min_volume) const override;
  int32_t SpeakerMuteIsAvailable(bool* available) override;
  int32_t SetSpeakerMute(bool enable) override;
  int32_t SpeakerMute(bool* enabled) const override;
  int32_t StereoPlayoutIsAvailable(bool* available) const override;
  int32_t SetStereoPlayout(bool enable) override;
  int32_t StereoPlayout(bool* enabled) const override;
  int32_t PlayoutDelay(uint16_t* delay_ms) const override;
  int32_t GetPlayoutUnderrunCount() const override;
  int32_t MicrophoneVolumeIsAvailable(bool* available) override;
  int32_t SetMicrophoneVolume(uint32_t volume) override;
  int32_t MicrophoneVolume(uint32_t* volume) const override;
  int32_t MaxMicrophoneVolume(uint32_t* max_volume) const override;
  int32_t MinMicrophoneVolume(uint32_t* min_volume) const override;
  int32_t MicrophoneMuteIsAvailable(bool* available) override;
  int32_t SetMicrophoneMute(bool enable) override;
  int32_t MicrophoneMute(bool* enabled) const override;
  bool BuiltInAECIsAvailable() const override;
  bool BuiltInAGCIsAvailable() const override;
  bool BuiltInNSIsAvailable() const override;
  int32_t EnableBuiltInAEC(bool enable) override;
  int32_t EnableBuiltInAGC(bool enable) override;
  int32_t EnableBuiltInNS(bool enable) override;

  Json stats() const;
  const std::string& last_error() const { return last_error_; }

 private:
  webrtc::scoped_refptr<webrtc::AudioDeviceModule> mic_;
  LoopbackAudioCapture loopback_;
  std::atomic<webrtc::AudioTransport*> transport_{nullptr};
  std::atomic<bool> initialized_{false};
  bool system_audio_ = false;
  std::string last_error_;
};

}  // namespace talk

#endif  // TALK_MEDIA_WEBRTC_SWITCHABLE_AUDIO_DEVICE_H_
