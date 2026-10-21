// File: core/base/event.h
// Brief: 核心事件到 C 回调的串行出口。
#ifndef TALK_BASE_EVENT_H_
#define TALK_BASE_EVENT_H_

#include <atomic>
#include <string>

#include "base/json.h"
#include "talk/talk.h"

namespace talk {

// 只在控制线程调用 Emit；回调异常在 C 边界内消化。
class EventSink {
 public:
  EventSink(talk_event_fn callback, void* userdata)
      : callback_(callback), userdata_(userdata) {}

  EventSink(const EventSink&) = delete;
  EventSink& operator=(const EventSink&) = delete;

  void Emit(const std::string& name, const Json& data) {
    if (stopped_ || callback_ == nullptr) {
      return;
    }
    const std::string text = data.dump();
    try {
      callback_(userdata_, name.c_str(), text.c_str());
    } catch (...) {
      // 回调属于调用方代码，异常不能穿过 core。
    }
  }

  void set_stopped(bool stopped) { stopped_ = stopped; }

 private:
  talk_event_fn callback_ = nullptr;
  void* userdata_ = nullptr;
  std::atomic<bool> stopped_{false};
};

}  // namespace talk

#endif  // TALK_BASE_EVENT_H_
