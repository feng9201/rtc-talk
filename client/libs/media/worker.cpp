#include "rtc_client/media/worker.h"
#include <atomic>

namespace rtc_client::media {
struct AudioWorker::State {
  asio::any_io_executor control;
  std::shared_ptr<MicrophoneGate> gate;
  Audio::Event event;
  AudioFactory factory;
  const std::size_t capacity;
  std::atomic<std::size_t> pending{0};
  std::atomic<bool> stopped{false};
  std::unique_ptr<Audio> audio;
  State(asio::any_io_executor executor, std::shared_ptr<MicrophoneGate> g,
        Audio::Event e, AudioFactory f, std::size_t limit)
      : control(std::move(executor)), gate(std::move(g)), event(std::move(e)),
        factory(std::move(f)), capacity(limit) {}
};
AudioWorker::AudioWorker(asio::any_io_executor control, std::shared_ptr<MicrophoneGate> gate,
                         Audio::Event event, AudioFactory factory, std::size_t capacity)
    : state_(std::make_shared<State>(std::move(control), std::move(gate),
                                     std::move(event), std::move(factory), capacity)) {
  if (!state_->gate || !state_->factory || !capacity) throw std::invalid_argument("audio worker configuration");
}
AudioWorker::~AudioWorker() { stop(); pool_.join(); }
bool AudioWorker::enqueue(std::function<Json(State&)> work, Done done) {
  auto state = state_;
  if (state->stopped || state->pending.load() >= state->capacity) return false;
  ++state->pending;
  try {
    asio::post(pool_, [state, work = std::move(work), done = std::move(done)]() mutable {
      Result result;
      try {
        if (state->stopped) result.code = 487;
        else result.data = work(*state);
      } catch (const Error& error) { state->gate->close(); result.code = error.code; }
        catch (...) { state->gate->close(); result.code = 500; }
      --state->pending;
      asio::post(state->control, [done = std::move(done), result = std::move(result)]() mutable {
        if (done) done(std::move(result));
      });
    });
  } catch (...) { --state->pending; throw; }
  return true;
}
bool AudioWorker::initialize(AudioOptions options, Json ice, Done done) {
  std::weak_ptr<State> weak = state_;
  return enqueue([weak, options = std::move(options), ice = std::move(ice)](State& state) {
    if (state.audio) throw Error(409);
    state.audio = state.factory(options, ice, state.gate,
        [weak](std::string name, Json data) {
          if (auto owner = weak.lock()) {
            if (owner->stopped) return;
            asio::post(owner->control, [weak, name = std::move(name), data = std::move(data)] {
              if (auto current = weak.lock())
                if (!current->stopped && current->event) current->event(name, data);
            });
          }
        });
    if (!state.audio) throw Error(503);
    return state.audio->devices();
  }, std::move(done));
}
bool AudioWorker::submit(Job work, Done done) {
  return enqueue([work = std::move(work)](State& state) {
    if (!state.audio) throw Error(409);
    return work(*state.audio);
  }, std::move(done));
}
void AudioWorker::stop() noexcept {
  state_->gate->close();
  if (state_->stopped.exchange(true)) return;
  try { asio::post(pool_, [state = state_] { state->audio.reset(); }); }
  catch (...) { std::terminate(); }  // Cannot destroy a live SDK on the control thread.
}
}  // namespace rtc_client::media
