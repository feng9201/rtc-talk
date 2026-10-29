#include "rtcwin/webrtc_media/switchable_audio_device.h"

#include <utility>

namespace rtcwin::webrtc_media {

SwitchableAudioDevice::SwitchableAudioDevice(
    webrtc::scoped_refptr<webrtc::AudioDeviceModule> mic, bool system_audio)
    : mic_(std::move(mic)), system_audio_(system_audio) {}

SwitchableAudioDevice::~SwitchableAudioDevice() { Terminate(); }

int32_t SwitchableAudioDevice::RegisterAudioCallback(
    webrtc::AudioTransport* transport) {
  transport_ = transport;
  return mic_ ? mic_->RegisterAudioCallback(transport) : -1;
}

int32_t SwitchableAudioDevice::Init() {
  if (initialized_) return 0;
  if (!mic_ || mic_->Init() != 0) {
    last_error_ = "麦克风/播放设备初始化失败";
    return -1;
  }
  initialized_ = true;
  return 0;
}

bool SwitchableAudioDevice::EnsureLoopback() {
  if (loopback_.initialized()) return true;
  std::string error;
  if (!loopback_.Init(&error)) {
    last_error_ = error;
    return false;
  }
  return true;
}

int32_t SwitchableAudioDevice::Terminate() {
  if (!initialized_) {
    loopback_.Terminate();
    return 0;
  }
  loopback_.Terminate();
  if (mic_) mic_->Terminate();
  initialized_ = false;
  return 0;
}

bool SwitchableAudioDevice::Initialized() const { return initialized_; }

int32_t SwitchableAudioDevice::ActiveAudioLayer(
    AudioLayer* audio_layer) const {
  return mic_ ? mic_->ActiveAudioLayer(audio_layer) : -1;
}

int32_t SwitchableAudioDevice::InitRecording() {
  if (system_audio_) return EnsureLoopback() ? 0 : -1;
  if (!mic_) return -1;
  const int32_t result = mic_->InitRecording();
  mic_inited_ = result == 0;
  return result;
}

int32_t SwitchableAudioDevice::StartRecording() {
  recording_requested_ = true;
  if (system_audio_) {
    if (!EnsureLoopback()) return -1;
    std::string error;
    if (!loopback_.Start(transport_.load(), &error)) {
      last_error_ = error.empty() ? "系统音启动采集失败" : error;
      return -1;
    }
    return 0;
  }
  if (!mic_) {
    last_error_ = "麦克风不可用";
    return -1;
  }
  if (!mic_inited_) {
    if (mic_->InitRecording() != 0) {
      last_error_ = "麦克风采集初始化失败";
      return -1;
    }
    mic_inited_ = true;
  }
  if (mic_->StartRecording() != 0) {
    last_error_ = "麦克风启动采集失败";
    mic_inited_ = false;
    return -1;
  }
  return 0;
}

int32_t SwitchableAudioDevice::StopRecording() {
  recording_requested_ = false;
  if (system_audio_) {
    loopback_.Stop();
    return 0;
  }
  if (!mic_) return -1;
  const int32_t result = mic_->StopRecording();
  mic_inited_ = false;
  return result;
}

bool SwitchableAudioDevice::RecordingIsInitialized() const {
  if (system_audio_) return loopback_.initialized();
  return mic_ ? mic_->RecordingIsInitialized() : false;
}

bool SwitchableAudioDevice::Recording() const {
  if (system_audio_) return loopback_.running();
  return mic_ ? mic_->Recording() : false;
}

int32_t SwitchableAudioDevice::SetRecordingDevice(uint16_t index) {
  return mic_ ? mic_->SetRecordingDevice(index) : -1;
}

int32_t SwitchableAudioDevice::SetRecordingDevice(WindowsDeviceType device) {
  return mic_ ? mic_->SetRecordingDevice(device) : -1;
}

int32_t SwitchableAudioDevice::SetStereoRecording(bool enable) {
  return system_audio_ ? 0 : (mic_ ? mic_->SetStereoRecording(enable) : -1);
}

int32_t SwitchableAudioDevice::StereoRecording(bool* enabled) const {
  if (system_audio_) {
    if (enabled) *enabled = false;
    return 0;
  }
  return mic_ ? mic_->StereoRecording(enabled) : -1;
}

int32_t SwitchableAudioDevice::StereoRecordingIsAvailable(
    bool* available) const {
  if (system_audio_) {
    if (available) *available = false;
    return 0;
  }
  return mic_ ? mic_->StereoRecordingIsAvailable(available) : -1;
}

int16_t SwitchableAudioDevice::RecordingDevices() {
  return mic_ ? mic_->RecordingDevices() : 0;
}

int32_t SwitchableAudioDevice::RecordingDeviceName(
    uint16_t index, char name[webrtc::kAdmMaxDeviceNameSize],
    char guid[webrtc::kAdmMaxGuidSize]) {
  return mic_ ? mic_->RecordingDeviceName(index, name, guid) : -1;
}

int32_t SwitchableAudioDevice::RecordingIsAvailable(bool* available) {
  return mic_ ? mic_->RecordingIsAvailable(available) : -1;
}

int32_t SwitchableAudioDevice::InitMicrophone() {
  return system_audio_ ? 0 : (mic_ ? mic_->InitMicrophone() : -1);
}

bool SwitchableAudioDevice::MicrophoneIsInitialized() const {
  return !system_audio_ && mic_ && mic_->MicrophoneIsInitialized();
}

int16_t SwitchableAudioDevice::PlayoutDevices() {
  return mic_ ? mic_->PlayoutDevices() : 0;
}

int32_t SwitchableAudioDevice::PlayoutDeviceName(
    uint16_t index, char name[webrtc::kAdmMaxDeviceNameSize],
    char guid[webrtc::kAdmMaxGuidSize]) {
  return mic_ ? mic_->PlayoutDeviceName(index, name, guid) : -1;
}

int32_t SwitchableAudioDevice::SetPlayoutDevice(uint16_t index) {
  return mic_ ? mic_->SetPlayoutDevice(index) : -1;
}

int32_t SwitchableAudioDevice::SetPlayoutDevice(WindowsDeviceType device) {
  return mic_ ? mic_->SetPlayoutDevice(device) : -1;
}

int32_t SwitchableAudioDevice::PlayoutIsAvailable(bool* available) {
  return mic_ ? mic_->PlayoutIsAvailable(available) : -1;
}

int32_t SwitchableAudioDevice::InitPlayout() {
  return mic_ ? mic_->InitPlayout() : -1;
}

bool SwitchableAudioDevice::PlayoutIsInitialized() const {
  return mic_ && mic_->PlayoutIsInitialized();
}

int32_t SwitchableAudioDevice::StartPlayout() {
  return mic_ ? mic_->StartPlayout() : -1;
}

int32_t SwitchableAudioDevice::StopPlayout() {
  return mic_ ? mic_->StopPlayout() : -1;
}

bool SwitchableAudioDevice::Playing() const {
  return mic_ && mic_->Playing();
}

int32_t SwitchableAudioDevice::InitSpeaker() {
  return mic_ ? mic_->InitSpeaker() : -1;
}

bool SwitchableAudioDevice::SpeakerIsInitialized() const {
  return mic_ && mic_->SpeakerIsInitialized();
}

int32_t SwitchableAudioDevice::SpeakerVolumeIsAvailable(bool* available) {
  return mic_ ? mic_->SpeakerVolumeIsAvailable(available) : -1;
}

int32_t SwitchableAudioDevice::SetSpeakerVolume(uint32_t volume) {
  return mic_ ? mic_->SetSpeakerVolume(volume) : -1;
}

int32_t SwitchableAudioDevice::SpeakerVolume(uint32_t* volume) const {
  return mic_ ? mic_->SpeakerVolume(volume) : -1;
}

int32_t SwitchableAudioDevice::MaxSpeakerVolume(uint32_t* max_volume) const {
  return mic_ ? mic_->MaxSpeakerVolume(max_volume) : -1;
}

int32_t SwitchableAudioDevice::MinSpeakerVolume(uint32_t* min_volume) const {
  return mic_ ? mic_->MinSpeakerVolume(min_volume) : -1;
}

int32_t SwitchableAudioDevice::SpeakerMuteIsAvailable(bool* available) {
  return mic_ ? mic_->SpeakerMuteIsAvailable(available) : -1;
}

int32_t SwitchableAudioDevice::SetSpeakerMute(bool enable) {
  return mic_ ? mic_->SetSpeakerMute(enable) : -1;
}

int32_t SwitchableAudioDevice::SpeakerMute(bool* enabled) const {
  return mic_ ? mic_->SpeakerMute(enabled) : -1;
}

int32_t SwitchableAudioDevice::StereoPlayoutIsAvailable(
    bool* available) const {
  return mic_ ? mic_->StereoPlayoutIsAvailable(available) : -1;
}

int32_t SwitchableAudioDevice::SetStereoPlayout(bool enable) {
  return mic_ ? mic_->SetStereoPlayout(enable) : -1;
}

int32_t SwitchableAudioDevice::StereoPlayout(bool* enabled) const {
  return mic_ ? mic_->StereoPlayout(enabled) : -1;
}

int32_t SwitchableAudioDevice::PlayoutDelay(uint16_t* delay_ms) const {
  return mic_ ? mic_->PlayoutDelay(delay_ms) : -1;
}

int32_t SwitchableAudioDevice::GetPlayoutUnderrunCount() const {
  return mic_ ? mic_->GetPlayoutUnderrunCount() : -1;
}

int32_t SwitchableAudioDevice::MicrophoneVolumeIsAvailable(bool* available) {
  return mic_ ? mic_->MicrophoneVolumeIsAvailable(available) : -1;
}

int32_t SwitchableAudioDevice::SetMicrophoneVolume(uint32_t volume) {
  return mic_ ? mic_->SetMicrophoneVolume(volume) : -1;
}

int32_t SwitchableAudioDevice::MicrophoneVolume(uint32_t* volume) const {
  return mic_ ? mic_->MicrophoneVolume(volume) : -1;
}

int32_t SwitchableAudioDevice::MaxMicrophoneVolume(
    uint32_t* max_volume) const {
  return mic_ ? mic_->MaxMicrophoneVolume(max_volume) : -1;
}

int32_t SwitchableAudioDevice::MinMicrophoneVolume(
    uint32_t* min_volume) const {
  return mic_ ? mic_->MinMicrophoneVolume(min_volume) : -1;
}

int32_t SwitchableAudioDevice::MicrophoneMuteIsAvailable(bool* available) {
  return mic_ ? mic_->MicrophoneMuteIsAvailable(available) : -1;
}

int32_t SwitchableAudioDevice::SetMicrophoneMute(bool enable) {
  return mic_ ? mic_->SetMicrophoneMute(enable) : -1;
}

int32_t SwitchableAudioDevice::MicrophoneMute(bool* enabled) const {
  return mic_ ? mic_->MicrophoneMute(enabled) : -1;
}

bool SwitchableAudioDevice::BuiltInAECIsAvailable() const {
  return mic_ && mic_->BuiltInAECIsAvailable();
}

bool SwitchableAudioDevice::BuiltInAGCIsAvailable() const {
  return mic_ && mic_->BuiltInAGCIsAvailable();
}

bool SwitchableAudioDevice::BuiltInNSIsAvailable() const {
  return mic_ && mic_->BuiltInNSIsAvailable();
}

int32_t SwitchableAudioDevice::EnableBuiltInAEC(bool enable) {
  return mic_ ? mic_->EnableBuiltInAEC(enable) : -1;
}

int32_t SwitchableAudioDevice::EnableBuiltInAGC(bool enable) {
  return mic_ ? mic_->EnableBuiltInAGC(enable) : -1;
}

int32_t SwitchableAudioDevice::EnableBuiltInNS(bool enable) {
  return mic_ ? mic_->EnableBuiltInNS(enable) : -1;
}

}  // namespace rtcwin::webrtc_media
