// File: core/media/webrtc/switchable_audio_device.cpp
// Brief: 实现麦克风/系统音切换 ADM 的播放转发与录音源选择。
#include "media/webrtc/switchable_audio_device.h"

#include <string>
#include <utility>

#include "rtc_base/logging.h"

namespace talk {

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
  if (initialized_) {
    return 0;
  }
  if (!mic_ || mic_->Init() != 0) {
    last_error_ = "麦克风/播放设备初始化失败";
    return -1;
  }
  // loopback 延迟到首次切到系统音时再初始化：没有播放设备的机器仍能用麦克风。
  initialized_ = true;
  return 0;
}

bool SwitchableAudioDevice::EnsureLoopback() {
  if (loopback_.initialized()) {
    return true;
  }
  std::string error;
  if (!loopback_.Init(&error)) {
    last_error_ = error;
    RTC_LOG(LS_WARNING) << "system audio init failed: " << error;
    return false;
  }
  return true;
}

int32_t SwitchableAudioDevice::SetAudioSource(bool system) {
  last_error_.clear();
  if (system == system_audio_) {
    // 同源重复设置：上层要求录音但实际未录（如设备热切换后线程退出），
    // 尝试补启，避免“看似成功实则静默”。
    if (recording_requested_ && !Recording()) {
      return StartRecording();
    }
    return 0;
  }
  const bool previous = system_audio_;
  const bool was_recording = recording_requested_ || Recording();
  if (was_recording) {
    StopRecording();
  }
  system_audio_ = system;
  if (system && !EnsureLoopback()) {
    // 回滚到原状态，避免处于“半切换”。
    system_audio_ = previous;
    if (was_recording) {
      StartRecording();
    }
    return -1;
  }
  if (!was_recording) {
    return 0;
  }
  if (StartRecording() == 0) {
    return 0;
  }
  // 新源无法启动：回到旧源并恢复录音，避免 ADM 与上层状态分裂。
  system_audio_ = previous;
  if (previous) {
    EnsureLoopback();
  }
  StartRecording();
  return -1;
}

int32_t SwitchableAudioDevice::Terminate() {
  loopback_.Terminate();
  if (mic_) {
    mic_->Terminate();
  }
  initialized_ = false;
  return 0;
}

bool SwitchableAudioDevice::Initialized() const { return initialized_; }

int32_t SwitchableAudioDevice::ActiveAudioLayer(
    AudioLayer* audio_layer) const {
  return mic_ ? mic_->ActiveAudioLayer(audio_layer) : -1;
}

int32_t SwitchableAudioDevice::InitRecording() {
  if (system_audio_) {
    return EnsureLoopback() ? 0 : -1;
  }
  if (!mic_) {
    return -1;
  }
  const int32_t result = mic_->InitRecording();
  mic_inited_ = result == 0;
  return result;
}

int32_t SwitchableAudioDevice::StartRecording() {
  recording_requested_ = true;
  if (system_audio_) {
    if (!EnsureLoopback()) {
      return -1;
    }
    std::string error;
    if (!loopback_.Start(transport_.load(), &error)) {
      last_error_ = error.empty() ? "系统音启动采集失败" : error;
      RTC_LOG(LS_WARNING) << "system audio start failed: " << error;
      return -1;
    }
    return 0;
  }
  if (!mic_) {
    last_error_ = "麦克风不可用";
    return -1;
  }
  if (!mic_inited_) {
    // 旧版 Windows Core ADM 的 StopRecording 会释放采集客户端并重置初始化状态，
    // 重新启动前必须重新 InitRecording。
    if (mic_->InitRecording() != 0) {
      last_error_ = "麦克风采集初始化失败";
      RTC_LOG(LS_WARNING) << "mic InitRecording failed";
      return -1;
    }
    mic_inited_ = true;
  }
  if (mic_->StartRecording() != 0) {
    last_error_ = "麦克风启动采集失败";
    mic_inited_ = false;
    RTC_LOG(LS_WARNING) << "mic StartRecording failed";
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
  if (!mic_) {
    return -1;
  }
  const int32_t result = mic_->StopRecording();
  // 旧 Core ADM 无论成败都会释放采集客户端，下次启动前需重新 InitRecording。
  mic_inited_ = false;
  return result;
}

bool SwitchableAudioDevice::RecordingIsInitialized() const {
  if (system_audio_) {
    return loopback_.initialized();
  }
  return mic_ ? mic_->RecordingIsInitialized() : false;
}

bool SwitchableAudioDevice::Recording() const {
  if (system_audio_) {
    return loopback_.running();
  }
  return mic_ ? mic_->Recording() : false;
}

int32_t SwitchableAudioDevice::SetRecordingDevice(uint16_t index) {
  // 设备选择只影响内置 ADM 的麦克风；系统音模式下也允许预选，
  // 以便切回麦克风时立即生效。
  return mic_ ? mic_->SetRecordingDevice(index) : -1;
}

int32_t SwitchableAudioDevice::SetRecordingDevice(
    WindowsDeviceType device) {
  return mic_ ? mic_->SetRecordingDevice(device) : -1;
}

int32_t SwitchableAudioDevice::SetStereoRecording(bool enable) {
  if (system_audio_) {
    return 0;
  }
  return mic_ ? mic_->SetStereoRecording(enable) : -1;
}

int32_t SwitchableAudioDevice::StereoRecording(bool* enabled) const {
  if (system_audio_) {
    if (enabled != nullptr) {
      *enabled = false;
    }
    return 0;
  }
  return mic_ ? mic_->StereoRecording(enabled) : -1;
}

int32_t SwitchableAudioDevice::StereoRecordingIsAvailable(
    bool* available) const {
  if (system_audio_) {
    if (available != nullptr) {
      *available = false;
    }
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
  if (system_audio_) {
    return 0;
  }
  return mic_ ? mic_->InitMicrophone() : -1;
}

bool SwitchableAudioDevice::MicrophoneIsInitialized() const {
  if (system_audio_) {
    return false;
  }
  return mic_ ? mic_->MicrophoneIsInitialized() : false;
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
  return mic_ ? mic_->PlayoutIsInitialized() : false;
}

int32_t SwitchableAudioDevice::StartPlayout() {
  return mic_ ? mic_->StartPlayout() : -1;
}

int32_t SwitchableAudioDevice::StopPlayout() {
  return mic_ ? mic_->StopPlayout() : -1;
}

bool SwitchableAudioDevice::Playing() const {
  return mic_ ? mic_->Playing() : false;
}

int32_t SwitchableAudioDevice::InitSpeaker() {
  return mic_ ? mic_->InitSpeaker() : -1;
}

bool SwitchableAudioDevice::SpeakerIsInitialized() const {
  return mic_ ? mic_->SpeakerIsInitialized() : false;
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

int32_t SwitchableAudioDevice::MaxSpeakerVolume(
    uint32_t* max_volume) const {
  return mic_ ? mic_->MaxSpeakerVolume(max_volume) : -1;
}

int32_t SwitchableAudioDevice::MinSpeakerVolume(
    uint32_t* min_volume) const {
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

int32_t SwitchableAudioDevice::MicrophoneVolumeIsAvailable(
    bool* available) {
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
  return mic_ ? mic_->BuiltInAECIsAvailable() : false;
}

bool SwitchableAudioDevice::BuiltInAGCIsAvailable() const {
  return mic_ ? mic_->BuiltInAGCIsAvailable() : false;
}

bool SwitchableAudioDevice::BuiltInNSIsAvailable() const {
  return mic_ ? mic_->BuiltInNSIsAvailable() : false;
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

Json SwitchableAudioDevice::stats() const {
  return {{"source", system_audio_ ? "system" : "mic"},
          {"loopback", loopback_.stats()}};
}

}  // namespace talk
