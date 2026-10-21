// File: core/media/webrtc/loopback_audio_capture.h
// Brief: WASAPI loopback 采集默认播放设备的系统音。
#ifndef TALK_MEDIA_WEBRTC_LOOPBACK_AUDIO_CAPTURE_H_
#define TALK_MEDIA_WEBRTC_LOOPBACK_AUDIO_CAPTURE_H_

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "base/json.h"

namespace webrtc {
class AudioTransport;
}

namespace talk {

// 采集默认播放设备的最终混音，转换为 48 kHz 单声道 16-bit，按 10 ms
// 调用 AudioTransport::RecordedDataIsAvailable。仅支持 Windows 共享模式。
// Init/Start/Stop/Terminate 必须在已初始化 COM（MTA）的 worker 线程调用。
class LoopbackAudioCapture {
 public:
  LoopbackAudioCapture();
  ~LoopbackAudioCapture();

  LoopbackAudioCapture(const LoopbackAudioCapture&) = delete;
  LoopbackAudioCapture& operator=(const LoopbackAudioCapture&) = delete;

  bool Init(std::string* error);
  bool Start(webrtc::AudioTransport* transport, std::string* error);
  void Stop();
  void Terminate();

  bool initialized() const { return initialized_.load(); }
  bool running() const { return running_.load(); }
  Json stats() const;

 private:
  struct Impl;

  bool Open(std::string* error);
  void Close();
  void Run();
  bool Convert(const uint8_t* data, uint32_t frames, uint32_t flags,
               std::vector<int16_t>* out);
  void Push(const int16_t* samples, size_t count);
  void PushPending();
  void RecordError(long hr);

  std::unique_ptr<Impl> impl_;
  std::thread thread_;
  std::atomic<webrtc::AudioTransport*> transport_{nullptr};
  std::atomic<bool> initialized_{false};
  std::atomic<bool> running_{false};
  std::atomic<bool> stop_{false};
  std::atomic<uint64_t> frames_{0};
  std::atomic<uint64_t> bytes_{0};
  std::atomic<uint64_t> silent_packets_{0};
  std::atomic<int> rms_{0};
  std::atomic<long> last_error_{0};
  std::vector<int16_t> pending_;  // 仅采集线程访问；Stop 后由调用线程清空
};

}  // namespace talk

#endif  // TALK_MEDIA_WEBRTC_LOOPBACK_AUDIO_CAPTURE_H_
