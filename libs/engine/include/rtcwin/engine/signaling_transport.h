#pragma once

#include "rtcwin/signaling/json.h"
#include "rtcwin/signaling/signaling_client.h"

#include <asio.hpp>

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <thread>

namespace rtcwin::engine {

// Owns the signaling io_context, its worker thread and SignalingClient.
// All SignalingClient operations execute on the signaling thread. JSON values
// are copied/moved into that thread; no libwebrtc or Qt object crosses it.
class SignalingTransport final {
 public:
  using RequestCallback = std::function<void(signaling::Json)>;
  using ConnectFailure = std::function<void(int, std::string)>;
  using BeforeStop = std::function<void()>;

  SignalingTransport();
  ~SignalingTransport();

  SignalingTransport(const SignalingTransport&) = delete;
  SignalingTransport& operator=(const SignalingTransport&) = delete;

  void Post(std::function<void()> task);
  void Connect(const std::string& url, const std::string& token,
               signaling::SignalingClient::Handlers handlers,
               ConnectFailure on_failure);
  void Request(const std::string& method, const signaling::Json& data,
               RequestCallback callback);
  void Notify(const std::string& method, const signaling::Json& data);
  void Send(const signaling::Json& message);
  void Dispose();
  void Stop(BeforeStop before_stop = {});

  asio::io_context& context() { return io_context_; }

 private:
  asio::io_context io_context_;
  std::unique_ptr<asio::executor_work_guard<asio::io_context::executor_type>>
      work_guard_;
  std::thread thread_;
  std::unique_ptr<signaling::SignalingClient> client_;
  std::atomic<bool> stopping_{false};
};

}  // namespace rtcwin::engine
