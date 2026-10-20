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
  virtual Json offer() = 0;
  virtual void accept(const std::string& sdp) = 0;
  virtual Json answer(const std::string& sdp) = 0;
  virtual void mute(bool muted) = 0;
  virtual Json devices() = 0;
  virtual void volume(double gain) = 0;
  virtual Json stats() = 0;
};
using MuteGate = std::shared_ptr<std::atomic<bool>>;
std::unique_ptr<Media> make_media(const Json& config, const Json& ice, MuteGate gate, Media::Event event);
}
