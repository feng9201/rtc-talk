#pragma once
#include "rtc_client/domain/microphone_gate.h"
#include "rtc_client/protocol/message.h"
#include <functional>
#include <memory>
#include <stdexcept>

namespace rtc_client::media {
#if defined(__GNUC__)
#define RTC_AUDIO_API __attribute__((visibility("default")))
#else
#define RTC_AUDIO_API
#endif
using Json = protocol::Json;
struct AudioOptions {
  std::string backend{"alsa"}, recording_device, playout_device;
};
class RTC_AUDIO_API Error : public std::runtime_error {
 public:
  explicit Error(int code) : std::runtime_error("media operation failed"), code(code) {}
  const int code;
};
// Confined to ONE media worker. No SDK objects or raw SDP cross into UI events.
// This phase supports SFU Opus only; video is explicitly rejected, not removed
// from SDP. A3 installs a real external decoder before advertising video.
class Audio {
 public:
  using Event = std::function<void(std::string, Json)>;
  virtual ~Audio() = default;
  virtual Json offer() = 0;
  virtual void accept(const std::string& answer) = 0;
  virtual Json answer(const std::string& offer) = 0;
  virtual void mute(bool muted) = 0;
  virtual Json devices() = 0;
  virtual Json stats() = 0;
};
using AudioFactory = std::function<std::unique_ptr<Audio>(const AudioOptions&,
    const Json& ice, std::shared_ptr<MicrophoneGate>, Audio::Event)>;
RTC_AUDIO_API std::unique_ptr<Audio> make_webrtc_audio(const AudioOptions&, const Json& ice,
    std::shared_ptr<MicrophoneGate>, Audio::Event);
#undef RTC_AUDIO_API
}  // namespace rtc_client::media
