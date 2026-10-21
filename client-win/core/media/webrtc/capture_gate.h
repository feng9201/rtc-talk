// File: core/media/webrtc/capture_gate.h
// Brief: 采集帧的静音与计数处理。
#ifndef TALK_MEDIA_WEBRTC_CAPTURE_GATE_H_
#define TALK_MEDIA_WEBRTC_CAPTURE_GATE_H_

#include <atomic>
#include <memory>
#include <mutex>

#include "api/audio/audio_frame.h"
#include "api/audio/audio_frame_processor.h"
#include "media/media.h"

namespace talk {

// 在编码前执行安全静音，不等待 SDP 或设备操作队列。
class CaptureGate : public webrtc::AudioFrameProcessor {
 public:
  CaptureGate(MuteGate gate, std::atomic<uint64_t>* frames);
  ~CaptureGate() override = default;

  void Process(std::unique_ptr<webrtc::AudioFrame> frame) override;
  void SetSink(OnAudioFrameCallback sink) override;

 private:
  MuteGate gate_;
  std::atomic<uint64_t>* frames_ = nullptr;
  std::mutex mutex_;
  OnAudioFrameCallback sink_;
};

}  // namespace talk

#endif  // TALK_MEDIA_WEBRTC_CAPTURE_GATE_H_
