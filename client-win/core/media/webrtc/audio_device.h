// File: core/media/webrtc/audio_device.h
// Brief: Windows Core Audio 设备与合成设备的统一封装。
#ifndef TALK_MEDIA_WEBRTC_AUDIO_DEVICE_H_
#define TALK_MEDIA_WEBRTC_AUDIO_DEVICE_H_

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>

#include "api/audio/audio_device.h"
#include "api/environment/environment.h"
#include "base/json.h"
#include "media/webrtc/sine_audio_device.h"
#include "media/webrtc/switchable_audio_device.h"
#include "rtc_base/thread.h"
#include "rtc_base/win/scoped_com_initializer.h"

namespace talk {

// 设备选择、播放启动与统计都经由 worker 线程访问 ADM。
class AudioDevice {
 public:
  static std::unique_ptr<AudioDevice> Create(const std::string& backend,
                                             const std::string& audio_source,
                                             const webrtc::Environment& env,
                                             webrtc::Thread* worker,
                                             std::string* error);
  ~AudioDevice();

  AudioDevice(const AudioDevice&) = delete;
  AudioDevice& operator=(const AudioDevice&) = delete;

  webrtc::AudioDeviceModule* adm() const { return adm_.get(); }
  SineAudioDevice* fake() const { return fake_.get(); }
  bool is_fake() const { return fake_ != nullptr; }
  std::atomic<uint64_t>* capture_frames() { return &capture_frames_; }

  // 只收不发时 need_recording=false：没有麦克风的机器仍能播放对端声音。
  bool SelectDevices(bool need_recording, const Json& config,
                     std::string* error);
  // 运行时切换采集源（worker 线程执行 ADM 切换）。
  bool SetAudioSource(bool system, std::string* error);
  bool EnsurePlayout();
  Json Enumerate() const;
  Json Stats() const;

 private:
  explicit AudioDevice(webrtc::Thread* worker);

  bool Initialize(const std::string& backend,
                  const std::string& audio_source,
                  const webrtc::Environment& env, std::string* error);
  bool SelectRecordingDevice(const Json& config, std::string* error);
  bool SelectPlayoutDevice(const Json& config, std::string* error);

  webrtc::scoped_refptr<webrtc::AudioDeviceModule> adm_;
  webrtc::scoped_refptr<SwitchableAudioDevice> switchable_;
  webrtc::scoped_refptr<SineAudioDevice> fake_;
  std::unique_ptr<webrtc::ScopedCOMInitializer> com_;
  webrtc::Thread* worker_ = nullptr;
  std::atomic<uint64_t> capture_frames_{0};
  Json device_config_ = Json::object();  // 设备配置快照，切回麦克风时补选用
  std::string audio_source_ = "mic";
  std::string recording_name_;
  std::string mic_recording_name_;  // 麦克风模式下选定的采集设备名，切回时复用
  std::string playout_name_;
  bool mic_device_selected_ = false;  // 麦克风采集设备是否已选择
  bool recording_selected_ = false;
  bool playout_selected_ = false;
  bool playout_started_ = false;
  bool playout_error_ = false;
};

}  // namespace talk

#endif  // TALK_MEDIA_WEBRTC_AUDIO_DEVICE_H_
