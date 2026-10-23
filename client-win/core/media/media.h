// File: core/media/media.h
// Brief: 媒体引擎抽象接口；音视频轨道由实现按 TrackKind 扩展。
#ifndef TALK_MEDIA_MEDIA_H_
#define TALK_MEDIA_MEDIA_H_

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include "base/json.h"

namespace talk {

// 采集端共享的静音闸门；音频帧处理线程只读，控制线程写。
using MuteGate = std::shared_ptr<std::atomic<bool>>;

// 预留视频扩展：PeerConnection 的发送器按轨道类型管理。
enum class TrackKind {
  kAudio,
  kVideo,
};

inline const char* TrackKindName(TrackKind kind) {
  return kind == TrackKind::kVideo ? "video" : "audio";
}

// 一帧 BGRA 图像；mid 与 bgra 只在回调期间有效，上层需要自行拷贝。
struct VideoFrameView {
  const char* mid = nullptr;
  const uint8_t* bgra = nullptr;
  int width = 0;
  int height = 0;
  int stride = 0;
  int64_t timestamp_us = 0;
};

using VideoFrameSink = std::function<void(const VideoFrameView&)>;

// 所有方法只在媒体任务线程调用，不阻塞控制线程或界面线程。
class Media {
 public:
  using Event = std::function<void(std::string, Json)>;

  virtual ~Media() = default;

  // SFU：发布端生成 offer / 应用 answer；订阅端应用 offer 并生成 answer。
  virtual Json Offer() = 0;
  virtual Json Accept(const std::string& sdp) = 0;
  virtual Json Answer(const std::string& sdp) = 0;

  // P2P：offerer 生成 offer；answerer 收到对端 offer 后回 answer。
  virtual Json StartP2p(bool offerer) = 0;
  virtual Json AnswerP2p(const std::string& sdp) = 0;
  virtual Json AcceptP2p(const std::string& sdp) = 0;
  virtual bool AddP2pCandidate(const std::string& candidate,
                               const std::string& mid, int index) = 0;
  virtual void CloseP2p() = 0;

  virtual void SetMuted(bool muted) = 0;
  virtual void SetVolume(double gain) = 0;

  // 运行时切换音频采集源（"mic" / "system"）；默认无操作。
  virtual Json SetAudioSource(const std::string& source) {
    (void)source;
    return Json::object();
  }

  // 进入 P2P 后只暂停 SFU 收发，不销毁 PeerConnection：
  // mediasoup transport 只接受一次 DTLS connect，切回 SFU 时需复用。
  virtual void SfuPause(bool paused) = 0;

  // 视频启停：source 为 "camera"（摄像头）或 "screen"（屏幕共享）。
  // 不支持视频的构建返回错误而不抛异常。
  virtual Json StartVideo(const std::string& source = "screen") {
    (void)source;
    return {{"error", "当前构建不支持视频"}};
  }
  virtual Json StopVideo(const std::string& source = "screen") {
    (void)source;
    return Json::object();
  }
  // 下行视频帧回调；可跨线程，空 sink 表示取消。
  virtual void SetVideoSink(VideoFrameSink sink) { (void)sink; }

  virtual Json GetDevices() = 0;
  virtual Json GetStats() = 0;
};

// 配置了非 none 的音频后端即启用媒体；视频接入后在此一并判断。
inline bool IsMediaEnabled(const Json& config) {
  if (!config.is_object()) {
    return false;
  }
  const auto it = config.find("audio_backend");
  if (it == config.end() || !it->is_string()) {
    return false;
  }
  return it->get<std::string>() != "none";
}

std::unique_ptr<Media> MakeMedia(const Json& config, const Json& ice_servers,
                                 MuteGate gate, Media::Event event,
                                 VideoFrameSink video_sink);

}  // namespace talk

#endif  // TALK_MEDIA_MEDIA_H_
