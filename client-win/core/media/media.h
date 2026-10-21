// File: core/media/media.h
// Brief: 媒体引擎抽象接口；音视频轨道由实现按 TrackKind 扩展。
#ifndef TALK_MEDIA_MEDIA_H_
#define TALK_MEDIA_MEDIA_H_

#include <atomic>
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

  // 进入 P2P 后只暂停 SFU 收发，不销毁 PeerConnection：
  // mediasoup transport 只接受一次 DTLS connect，切回 SFU 时需复用。
  virtual void SfuPause(bool paused) = 0;

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
                                 MuteGate gate, Media::Event event);

}  // namespace talk

#endif  // TALK_MEDIA_MEDIA_H_
