// File: core/app/talk_client.cpp
// Brief: 实现客户端组合根与命令提交。
#include "app/talk_client.h"

#include <exception>
#include <future>
#include <utility>

#include "base/log.h"
#include "signaling/protocol.h"

namespace talk {
namespace {

// 防止界面线程刷命令拖垮控制线程。
constexpr int kMaxQueuedCommands = 256;

}  // namespace

TalkClient::TalkClient(talk_event_fn callback, void* userdata, Json config)
    : events_(callback, userdata),
      config_(std::move(config)),
      media_(runtime_.io(), config_, events_,
             [this](const VideoFrameView& view) { DeliverVideoFrame(view); }),
      signaling_(runtime_.io()),
      session_(runtime_.io(), config_, events_, signaling_, media_) {
  media_.SetSignaling(MediaSignaling{
      [this](std::string method, Json data) {
        signaling_.Notify(method, std::move(data));
      },
      [this](std::string method, Json data,
             std::function<void(Json)> done) {
        signaling_.Request(std::move(method), std::move(data),
                           std::move(done));
      }});
  TALK_LOG_INFO("app", "客户端启动 version={}", talk_version());
  TALK_LOG_INFO("app", "配置 backend={} mode={} record={} playout={}",
                config_.value("audio_backend", std::string("none")),
                config_.value("media_mode", std::string("auto")),
                config_.value("recording_device", std::string()),
                config_.value("playout_device", std::string()));
}

TalkClient::~TalkClient() {
  events_.set_stopped(true);
  TALK_LOG_INFO("app", "客户端开始关闭");
  // 媒体队列与信令都在控制线程收尾，避免与在途回调竞争。
  std::promise<void> barrier;
  std::future<void> finished = barrier.get_future();
  runtime_.Post([this, &barrier] {
    try {
      session_.Shutdown();
      media_.Shutdown();
    } catch (...) {
      // 析构路径不能抛出；确保等待方一定继续。
      TALK_LOG_ERROR("app", "关闭时捕获未知异常");
    }
    barrier.set_value();
  });
  finished.wait();
  runtime_.Stop();
  TALK_LOG_INFO("app", "客户端已关闭");
}

uint64_t TalkClient::Submit(std::string method, Json data) {
  if (queued_.fetch_add(1) >= kMaxQueuedCommands) {
    --queued_;
    TALK_LOG_WARN("app", "命令队列已满，拒绝 method={}", method);
    return 0;
  }
  const uint64_t id = next_operation_.fetch_add(1);
  TALK_LOG_DEBUG("app", "提交命令 id={} method={}", id, method);
  runtime_.Post([this, id, method = std::move(method),
                 data = std::move(data)]() mutable {
    --queued_;
    try {
      session_.Command(id, method, std::move(data));
    } catch (const std::exception& error) {
      TALK_LOG_ERROR("app", "命令处理异常 id={} method={} error={}", id,
                     method, error.what());
      session_.Complete(id, method, Failure("", 400, "参数或状态无效"));
    } catch (...) {
      TALK_LOG_ERROR("app", "命令处理异常 id={} method={}：未知异常",
                     id, method);
      session_.Complete(id, method, Failure("", 400, "参数或状态无效"));
    }
  });
  return id;
}

void TalkClient::SetVideoFrameCallback(talk_video_frame_fn callback,
                                     void* userdata) {
  std::lock_guard<std::mutex> lock(video_mutex_);
  video_callback_ = callback;
  video_userdata_ = callback != nullptr ? userdata : nullptr;
  if (callback != nullptr) {
    // 重新订阅后允许各路再记一次首帧。
    std::lock_guard<std::mutex> lock(video_first_frame_mutex_);
    video_first_frame_mids_.clear();
  }
  TALK_LOG_INFO("app", "视频帧回调已{}", callback != nullptr ? "注册" : "取消");
}

void TalkClient::DeliverVideoFrame(const VideoFrameView& view) {
  // 媒体线程：回调期间 bgra/mid 有效，必须先取副本再放锁，避免与替换回调互等。
  talk_video_frame_fn callback = nullptr;
  void* userdata = nullptr;
  {
    std::lock_guard<std::mutex> lock(video_mutex_);
    callback = video_callback_;
    userdata = video_userdata_;
  }
  if (callback == nullptr || view.bgra == nullptr || view.width <= 0 ||
      view.height <= 0) {
    return;
  }
  // 多路视频每路各记一次首帧，用来确认每个 mid 都真的解码出了画面。
  const std::string mid = view.mid != nullptr ? view.mid : "";
  bool first_frame = false;
  {
    std::lock_guard<std::mutex> lock(video_first_frame_mutex_);
    first_frame = video_first_frame_mids_.insert(mid).second;
  }
  if (first_frame) {
    TALK_LOG_INFO("app", "首个远端视频帧 mid={} {}x{} stride={}", mid,
                  view.width, view.height, view.stride);
  }
  callback(userdata, mid.c_str(), view.bgra, view.width, view.height,
           view.stride, view.timestamp_us);
}

}  // namespace talk
