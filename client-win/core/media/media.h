#pragma once
#include <nlohmann/json.hpp>
#include <functional>
#include <atomic>
#include <memory>
namespace talk {
using Json = nlohmann::json;
/* 所有方法只在媒体任务队列调用；不阻塞 Asio 或 GUI。 */
class Media {
 public:
  using Event = std::function<void(std::string, Json)>;
  virtual ~Media() = default;
  virtual Json offer() = 0;
  virtual void accept(const std::string& sdp) = 0;
  virtual Json answer(const std::string& sdp) = 0;
  virtual void mute(bool muted) = 0;
  virtual Json devices() = 0;
  virtual void volume(double gain) = 0;
  virtual Json stats() = 0;
  /* S3：1v1 P2P。offerer 生成 offer，answerer 收到对端 offer 后回 answer。
   * 本地候选与连接状态通过 p2p.candidate / p2p.state 事件上报。 */
  virtual Json p2p_start(bool offerer) = 0;
  virtual Json p2p_answer(const std::string& sdp) = 0;
  virtual void p2p_accept(const std::string& sdp) = 0;
  virtual bool p2p_candidate(const std::string& candidate, const std::string& mid, int index) = 0;
  virtual void p2p_close() = 0;
  /* 进入 P2P 后只静音 SFU 的两条 PC，不销毁：mediasoup transport 只接受一次
   * DTLS connect，切回 SFU 时必须复用同一对 PC 重新协商。 */
  virtual void sfu_pause(bool paused) = 0;
};
using MuteGate = std::shared_ptr<std::atomic<bool>>;
std::unique_ptr<Media> make_media(const Json& config, const Json& ice, MuteGate gate, Media::Event event);
}
