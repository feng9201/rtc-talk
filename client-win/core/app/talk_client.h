// File: core/app/talk_client.h
// Brief: 客户端组合根：控制线程、信令、媒体与会话的生命周期。
#ifndef TALK_APP_TALK_CLIENT_H_
#define TALK_APP_TALK_CLIENT_H_

#include <atomic>
#include <cstdint>
#include <string>

#include "base/event.h"
#include "base/io_runtime.h"
#include "base/json.h"
#include "media/media_controller.h"
#include "session/call_session.h"
#include "signaling/signaling_client.h"
#include "talk/talk.h"

namespace talk {

// talk.h 的 C ABI 背后实例；所有状态只在控制线程变更。
class TalkClient {
 public:
  TalkClient(talk_event_fn callback, void* userdata, Json config);
  ~TalkClient();

  TalkClient(const TalkClient&) = delete;
  TalkClient& operator=(const TalkClient&) = delete;

  // 返回操作 ID；0 表示命令队列已满。
  uint64_t Submit(std::string method, Json data);

 private:
  IoRuntime runtime_;
  EventSink events_;
  Json config_;
  MediaController media_;
  SignalingClient signaling_;
  CallSession session_;
  std::atomic<uint64_t> next_operation_{1};
  std::atomic<int> queued_{0};
};

}  // namespace talk

#endif  // TALK_APP_TALK_CLIENT_H_
