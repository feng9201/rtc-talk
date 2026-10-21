// File: core/media/webrtc/sine_audio_device.h
// Brief: 合成音频设备，用于无麦克风环境验证媒体链路。
#ifndef TALK_MEDIA_WEBRTC_SINE_AUDIO_DEVICE_H_
#define TALK_MEDIA_WEBRTC_SINE_AUDIO_DEVICE_H_

#include <atomic>
#include <cstdint>
#include <thread>

#include "api/audio/audio_device.h"
#include "base/json.h"
#include "modules/audio_device/include/audio_device_default.h"

namespace talk {

class SineAudioDevice
    : public webrtc::webrtc_impl::AudioDeviceModuleDefault<
          webrtc::AudioDeviceModule> {
 public:
  ~SineAudioDevice() override;

  int32_t RegisterAudioCallback(webrtc::AudioTransport* transport) override;
  int32_t StartRecording() override;
  int32_t StartPlayout() override;
  int32_t StopRecording() override;
  int32_t StopPlayout() override;
  bool Recording() const override;
  bool Playing() const override;
  int32_t Terminate() override;

  Json stats() const;

 private:
  void Start();
  void Run();

  std::atomic<webrtc::AudioTransport*> transport_{nullptr};
  std::atomic<bool> running_{false};
  std::atomic<bool> playing_{false};
  std::atomic<bool> recording_{false};
  std::atomic<int> frames_{0};
  std::atomic<int> signal_frames_{0};
  std::atomic<int> rms_{0};
  std::atomic<int> clipped_{0};
  std::atomic<double> frequency_{0};
  std::thread thread_;
};

}  // namespace talk

#endif  // TALK_MEDIA_WEBRTC_SINE_AUDIO_DEVICE_H_
