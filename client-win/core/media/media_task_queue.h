// File: core/media/media_task_queue.h
// Brief: 串行媒体任务队列；媒体实例按会话代次创建和销毁。
#ifndef TALK_MEDIA_MEDIA_TASK_QUEUE_H_
#define TALK_MEDIA_MEDIA_TASK_QUEUE_H_

#include <cstdint>
#include <functional>
#include <memory>
#include <string>

#include <asio.hpp>

#include "base/json.h"
#include "media/media.h"

namespace talk {

// 媒体任务结果；错误码只在失败时有效。
struct MediaResult {
  bool ok = true;
  Json data = Json::object();
  int code = 200;
  std::string error;
};

// 所有公开方法只在控制线程调用；媒体实例只在任务线程访问。
class MediaTaskQueue {
 public:
  using Job = std::function<Json(Media&)>;
  using Done = std::function<void(MediaResult)>;

  MediaTaskQueue(asio::io_context& io, const Json& config, MuteGate gate,
                 Media::Event event);
  ~MediaTaskQueue();

  MediaTaskQueue(const MediaTaskQueue&) = delete;
  MediaTaskQueue& operator=(const MediaTaskQueue&) = delete;

  void SetIceServers(const Json& ice_servers);
  void Post(Job job, Done done, bool create = true);

  // 控制线程：更新配置里的采集源，供后续重建媒体实例继承。
  void SetAudioSourceInConfig(const std::string& source);

  // 丢弃当前媒体实例；后续任务会创建新实例和新静音闸门。
  void Reset();

  // 在控制线程调用：等待任务线程退出后再返回。
  void Shutdown();

  MuteGate gate() const { return gate_; }
  bool enabled() const { return enabled_; }

 private:
  void Deliver(Done done, MediaResult result, uint64_t generation);

  asio::io_context& io_;
  asio::thread_pool pool_;
  Json config_;
  Json ice_servers_ = Json::array();
  MuteGate gate_;
  Media::Event event_;
  std::unique_ptr<Media> media_;
  uint64_t generation_ = 0;
  int queued_ = 0;
  bool enabled_ = false;
  bool stopping_ = false;
  bool joined_ = false;
};

}  // namespace talk

#endif  // TALK_MEDIA_MEDIA_TASK_QUEUE_H_
