// File: core/media/webrtc/wait_utils.h
// Brief: 等待 WebRTC 回调结果的通用超时工具。
#ifndef TALK_MEDIA_WEBRTC_WAIT_UTILS_H_
#define TALK_MEDIA_WEBRTC_WAIT_UTILS_H_

#include <chrono>
#include <future>
#include <utility>

namespace talk {

// 超时返回默认值；调用方需自行判断结果是否有效。
template <class T>
T WaitFor(std::future<T> future,
          std::chrono::milliseconds timeout = std::chrono::seconds(8)) {
  if (future.wait_for(timeout) != std::future_status::ready) {
    return T{};
  }
  return future.get();
}

}  // namespace talk

#endif  // TALK_MEDIA_WEBRTC_WAIT_UTILS_H_
