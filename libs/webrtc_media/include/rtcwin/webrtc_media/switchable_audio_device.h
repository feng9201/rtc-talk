#pragma once

#include "rtcwin/webrtc_media/loopback_audio_capture.h"

#include "api/audio/audio_device.h"
#include "modules/audio_device/include/audio_device_default.h"

#include <atomic>
#include <string>

namespace rtcwin::webrtc_media {

// Uses the Core Audio ADM for playout and microphone capture, and replaces
// recording with WASAPI loopback when system_audio is true.
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

  bool system_audio() const { return system_audio_; }
  const std::string& last_error() const { return last_error_; }

 private:
  bool EnsureLoopback();

  webrtc::scoped_refptr<webrtc::AudioDeviceModule> mic_;
  LoopbackAudioCapture loopback_;
  std::atomic<webrtc::AudioTransport*> transport_{nullptr};
  std::atomic<bool> initialized_{false};
  bool system_audio_ = false;
  bool recording_requested_ = false;
  bool mic_inited_ = false;
  std::string last_error_;
};

}  // namespace rtcwin::webrtc_media
