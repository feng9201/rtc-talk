// File: core/media/media_task_queue.cpp
// Brief: 实现媒体任务串行化、代次校验与生命周期管理。
#include "media/media_task_queue.h"

#include <exception>
#include <utility>

#include "base/log.h"

namespace talk {
namespace {

constexpr int kMaxQueuedMediaTasks = 64;

}  // namespace

MediaTaskQueue::MediaTaskQueue(asio::io_context& io, const Json& config,
                               MuteGate gate, Media::Event event,
                               VideoFrameSink video_sink)
    : io_(io),
      pool_(1),
      config_(config),
      gate_(std::move(gate)),
      event_(std::move(event)),
      video_sink_(std::move(video_sink)),
      enabled_(IsMediaEnabled(config)) {
  const std::string backend =
      config_.value("audio_backend", std::string("none"));
  if (enabled_) {
    TALK_LOG_INFO("media", "媒体队列初始化 enabled=true backend={}",
                  backend);
  } else {
    TALK_LOG_WARN("media",
                  "媒体未启用 backend={}：不会采集或播放音频", backend);
  }
  TALK_LOG_INFO("media", "媒体设备 record={} playout={}",
                config_.value("recording_device", std::string()),
                config_.value("playout_device", std::string()));
}

MediaTaskQueue::~MediaTaskQueue() { Shutdown(); }

void MediaTaskQueue::SetIceServers(const Json& ice_servers) {
  ice_servers_ = ice_servers;
  TALK_LOG_DEBUG("media", "更新 ICE 服务器 count={}",
                 ice_servers_.size());
}

void MediaTaskQueue::SetAudioSourceInConfig(const std::string& source) {
  config_["audio_source"] = source;
}

void MediaTaskQueue::Post(Job job, Done done, bool create) {
  if (stopping_) {
    TALK_LOG_DEBUG("media", "队列已停止，丢弃任务");
    return;
  }
  if (queued_ >= kMaxQueuedMediaTasks) {
    TALK_LOG_WARN("media", "媒体队列已满 queued={}", queued_);
    if (done) {
      done(MediaResult{false, Json::object(), 429, "媒体队列已满"});
    }
    return;
  }
  ++queued_;
  const uint64_t generation = generation_;
  Json config = config_;
  Json ice_servers = ice_servers_;
  MuteGate gate = gate_;
  asio::post(pool_, [this, generation, config = std::move(config),
                     ice_servers = std::move(ice_servers),
                     gate = std::move(gate), job = std::move(job),
                     done = std::move(done), create]() mutable {
    MediaResult result;
    try {
      if (!media_ && create) {
        media_ = MakeMedia(
            config, ice_servers, gate,
            [this, generation](std::string name, Json data) {
              asio::post(io_, [this, generation, name = std::move(name),
                               data = std::move(data)]() mutable {
                if (stopping_ || generation != generation_ || !event_) {
                  return;
                }
                event_(std::move(name), std::move(data));
              });
            },
            video_sink_);
      }
      if (!media_) {
        if (create) {
          TALK_LOG_ERROR("media", "创建媒体实例失败 backend={}",
                         config.value("audio_backend", std::string("none")));
          result = MediaResult{false, Json::object(), 500, "媒体初始化失败"};
        } else {
          result.data = Json::object();
        }
      } else {
        Json data = job(*media_);
        if (data.is_object() && data.contains("error") &&
            data["error"].is_string()) {
          TALK_LOG_ERROR("media", "媒体操作失败 error={}",
                         data["error"].get<std::string>());
          result = MediaResult{false, Json::object(), 500,
                               data["error"].get<std::string>()};
        } else {
          result.data = std::move(data);
        }
      }
    } catch (const std::exception& error) {
      TALK_LOG_ERROR("media", "媒体任务异常 error={}", error.what());
      result = MediaResult{false, Json::object(), 500,
                           "媒体操作失败，请检查设备或 SDP 兼容性"};
    } catch (...) {
      TALK_LOG_ERROR("media", "媒体任务异常：未知异常");
      result = MediaResult{false, Json::object(), 500,
                           "媒体操作失败，请检查设备或 SDP 兼容性"};
    }
    asio::post(io_, [this, generation, done = std::move(done),
                     result = std::move(result)]() mutable {
      --queued_;
      if (stopping_) {
        return;
      }
      if (generation != generation_) {
        TALK_LOG_DEBUG("media", "媒体结果已过期 generation={}",
                       generation);
        result = MediaResult{false, Json::object(), 487, "媒体操作已取消"};
      }
      if (done) {
        done(std::move(result));
      }
    });
  });
}

void MediaTaskQueue::Reset() {
  if (stopping_) {
    return;
  }
  TALK_LOG_INFO("media", "重置媒体实例");
  ++generation_;
  gate_ = std::make_shared<std::atomic<bool>>(true);
  asio::post(pool_, [this] { media_.reset(); });
}

void MediaTaskQueue::Shutdown() {
  if (joined_) {
    return;
  }
  TALK_LOG_INFO("media", "停止媒体队列");
  stopping_ = true;
  ++generation_;
  asio::post(pool_, [this] { media_.reset(); });
  pool_.join();
  joined_ = true;
  TALK_LOG_INFO("media", "媒体队列已停止");
}

}  // namespace talk
