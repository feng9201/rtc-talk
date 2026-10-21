// File: core/media/webrtc/capture_gate.cpp
// Brief: 实现采集帧静音与计数。
#include "media/webrtc/capture_gate.h"

#include <utility>

namespace talk {

CaptureGate::CaptureGate(MuteGate gate, std::atomic<uint64_t>* frames)
    : gate_(std::move(gate)), frames_(frames) {}

void CaptureGate::Process(std::unique_ptr<webrtc::AudioFrame> frame) {
  if (gate_->load()) {
    frame->Mute();
  }
  if (frames_ != nullptr) {
    frames_->fetch_add(1, std::memory_order_relaxed);
  }
  OnAudioFrameCallback sink;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    sink = sink_;
  }
  if (sink) {
    sink(std::move(frame));
  }
}

void CaptureGate::SetSink(OnAudioFrameCallback sink) {
  std::lock_guard<std::mutex> lock(mutex_);
  sink_ = std::move(sink);
}

}  // namespace talk
