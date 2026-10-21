// File: core/media/webrtc/sine_audio_device.cpp
// Brief: 实现合成音频设备。
#include "media/webrtc/sine_audio_device.h"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace talk {
namespace {

constexpr int kSampleRate = 48000;
constexpr int kChannels = 1;
constexpr int kBytesPerSample = 2;
constexpr int kSamplesPerFrame = 480;
constexpr int kFrameMs = 10;
constexpr int kToneFrequencyHz = 440;
constexpr int kToneAmplitude = 3000;
constexpr int kSignalThreshold = 100;

}  // namespace

SineAudioDevice::~SineAudioDevice() { Terminate(); }

int32_t SineAudioDevice::RegisterAudioCallback(
    webrtc::AudioTransport* transport) {
  transport_ = transport;
  return 0;
}

int32_t SineAudioDevice::StartRecording() {
  recording_ = true;
  Start();
  return 0;
}

int32_t SineAudioDevice::StartPlayout() {
  playing_ = true;
  Start();
  return 0;
}

int32_t SineAudioDevice::StopRecording() {
  recording_ = false;
  return 0;
}

int32_t SineAudioDevice::StopPlayout() {
  playing_ = false;
  return 0;
}

bool SineAudioDevice::Recording() const { return recording_; }

bool SineAudioDevice::Playing() const { return playing_; }

int32_t SineAudioDevice::Terminate() {
  running_ = false;
  if (thread_.joinable()) {
    thread_.join();
  }
  return 0;
}

Json SineAudioDevice::stats() const {
  return {{"frames", frames_.load()},
          {"signal_frames", signal_frames_.load()},
          {"rms", rms_.load()},
          {"clipped", clipped_.load()},
          {"frequency_hz", frequency_.load()}};
}

void SineAudioDevice::Start() {
  if (thread_.joinable()) {
    return;
  }
  running_ = true;
  thread_ = std::thread([this] { Run(); });
}

void SineAudioDevice::Run() {
  uint64_t sample = 0;
  auto next = std::chrono::steady_clock::now();
  int crossings = 0;
  int measured = 0;
  int16_t previous = 0;
  while (running_) {
    next += std::chrono::milliseconds(kFrameMs);
    int16_t buffer[kSamplesPerFrame]{};
    auto* transport = transport_.load();
    if (transport != nullptr && recording_) {
      for (auto& value : buffer) {
        const double phase = 6.283185307179586 *
                             kToneFrequencyHz *
                             static_cast<double>(sample++) / kSampleRate;
        value = static_cast<int16_t>(kToneAmplitude * std::sin(phase));
      }
      uint32_t level = 0;
      transport->RecordedDataIsAvailable(
          buffer, kSamplesPerFrame, kBytesPerSample, kChannels, kSampleRate, 0,
          0, 0, false, level);
    }
    if (transport != nullptr && playing_) {
      size_t out = 0;
      int64_t elapsed = 0;
      int64_t ntp = 0;
      transport->NeedMorePlayData(kSamplesPerFrame, kBytesPerSample, kChannels,
                                  kSampleRate, buffer, out, &elapsed, &ntp);
      out = std::min(out, static_cast<size_t>(kSamplesPerFrame));
      double energy = 0;
      for (size_t i = 0; i < out; ++i) {
        energy += static_cast<double>(buffer[i]) * buffer[i];
        if (buffer[i] == INT16_MAX || buffer[i] == INT16_MIN) {
          ++clipped_;
        }
        if (previous <= 0 && buffer[i] > 0) {
          ++crossings;
        }
        previous = buffer[i];
      }
      rms_ = out != 0 ? static_cast<int>(std::sqrt(energy / out)) : 0;
      if (rms_ > kSignalThreshold) {
        ++signal_frames_;
      }
      ++frames_;
      measured += static_cast<int>(out);
      if (measured >= kSampleRate) {
        frequency_ = static_cast<double>(crossings) * kSampleRate / measured;
        crossings = 0;
        measured = 0;
      }
    }
    std::this_thread::sleep_until(next);
  }
}

}  // namespace talk
