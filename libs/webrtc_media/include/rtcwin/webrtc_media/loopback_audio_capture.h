#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace webrtc {
class AudioTransport;
}

namespace rtcwin::webrtc_media {

// Captures the default Windows render endpoint in shared loopback mode and
// delivers 48 kHz mono signed-16 PCM in 10 ms frames through AudioTransport.
// Init/Start/Stop/Terminate must be called on the COM MTA worker thread.
class LoopbackAudioCapture final {
 public:
  LoopbackAudioCapture();
  ~LoopbackAudioCapture();

  LoopbackAudioCapture(const LoopbackAudioCapture&) = delete;
  LoopbackAudioCapture& operator=(const LoopbackAudioCapture&) = delete;

  bool Init(std::string* error);
  bool Init(const std::string& device_id, std::string* error);
  bool SetDevice(const std::string& device_id, std::string* error);
  bool Start(webrtc::AudioTransport* transport, std::string* error);
  void Stop();
  void Terminate();

  bool initialized() const { return initialized_.load(); }
  bool running() const { return running_.load(); }
  std::uint64_t frames() const { return frames_.load(); }
  std::uint64_t bytes() const { return bytes_.load(); }
  int rms() const { return rms_.load(); }

 private:
  struct Impl;

  bool Open(std::string* error);
  void Close();
  bool Reopen();
  void Run();
  bool Convert(const std::uint8_t* data, std::uint32_t frames,
               std::uint32_t flags, std::vector<std::int16_t>* out);
  void Push(const std::int16_t* samples, std::size_t count);
  void PushPending();
  void RecordError(long hr);

  std::unique_ptr<Impl> impl_;
  std::thread thread_;
  std::atomic<webrtc::AudioTransport*> transport_{nullptr};
  std::atomic<bool> initialized_{false};
  std::atomic<bool> running_{false};
  std::atomic<bool> stop_{false};
  std::atomic<std::uint64_t> frames_{0};
  std::atomic<std::uint64_t> bytes_{0};
  std::atomic<int> rms_{0};
  std::atomic<long> last_error_{0};
  std::vector<std::int16_t> pending_;
  bool follow_default_ = true;
};

}  // namespace rtcwin::webrtc_media
