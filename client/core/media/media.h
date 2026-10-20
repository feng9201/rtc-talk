#pragma once
#include "signaling/protocol.h"
#include <functional>
#include <atomic>
#include <memory>
namespace talk {
/* 所有方法只在媒体任务队列调用；不阻塞 Asio 或 GUI。 */
class Media {
 public:
  using Event = std::function<void(std::string, Json)>;
  virtual ~Media() = default;
  /* SFU 发送/接收 PC。 */
  virtual Json offer() = 0;
  virtual void accept(const std::string& sdp) = 0;
  virtual Json answer(const std::string& sdp) = 0;
  virtual void mute(bool muted) = 0;
  virtual Json devices() = 0;
  virtual void volume(double gain) = 0;
  virtual Json stats() = 0;
  /* P2P：与 SFU 并存的 sendrecv 音频链路，候选经事件回调输出。 */
  virtual Json p2p_offer() = 0;
  virtual void p2p_accept(const std::string& sdp) = 0;
  virtual Json p2p_answer(const std::string& sdp) = 0;
  virtual void p2p_candidate(const std::string& candidate, const std::string& mid, int index) = 0;
  virtual void p2p_close() = 0;
  /* 选中候选对的路径：host/srflx/relay；未连通时返回空对象。 */
  virtual Json p2p_path() = 0;
  /* 模式切换：暂停 SFU 上行/下行（P2P 期间只保留一路播放）。 */
  virtual void sfu_send(bool enabled) = 0;
  virtual void sfu_playback(bool enabled) = 0;
  /* 按 TTL 刷新后的 ICE servers；新 PC 直接使用，已有 PC 尝试热更新。 */
  virtual void update_ice(const Json& ice) = 0;
};
using MuteGate = std::shared_ptr<std::atomic<bool>>;
std::unique_ptr<Media> make_media(const Json& config, const Json& ice, MuteGate gate, Media::Event event);
}
