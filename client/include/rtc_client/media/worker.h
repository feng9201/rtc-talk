#pragma once
#include "rtc_client/media/audio.h"
#include <asio.hpp>

namespace rtc_client::media {
struct Result {
  int code{};
  Json data = Json::object();
  bool ok() const noexcept { return code == 0; }
};
// Control-executor confined submission. Bounded queue, independent of WS and GUI.
// Stop closes the capture gate immediately, cancels queued work and destroys the
// backend on its owning worker. Destruction joins; never detach a live SDK.
class AudioWorker final {
 public:
  using Done = std::function<void(Result)>;
  using Job = std::function<Json(Audio&)>;
  AudioWorker(asio::any_io_executor control, std::shared_ptr<MicrophoneGate> gate,
              Audio::Event event, AudioFactory factory, std::size_t capacity = 8);
  ~AudioWorker();
  AudioWorker(const AudioWorker&) = delete;
  AudioWorker& operator=(const AudioWorker&) = delete;
  bool initialize(AudioOptions, Json ice, Done);
  bool submit(Job, Done);
  void stop() noexcept;
 private:
  struct State;
  bool enqueue(std::function<Json(State&)>, Done);
  std::shared_ptr<State> state_;
  asio::thread_pool pool_{1};
};
}  // namespace rtc_client::media
