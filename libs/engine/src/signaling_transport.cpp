#include "rtcwin/engine/signaling_transport.h"

#include "rtcwin/signaling/protocol.h"

#include <chrono>
#include <future>
#include <utility>

namespace rtcwin::engine {

using signaling::Json;

SignalingTransport::SignalingTransport() {
  work_guard_ = std::make_unique<
      asio::executor_work_guard<asio::io_context::executor_type>>(
      asio::make_work_guard(io_context_));
  thread_ = std::thread([this] {
    client_ = std::make_unique<signaling::SignalingClient>(io_context_);
    io_context_.run();
  });
}

SignalingTransport::~SignalingTransport() { Stop(); }

void SignalingTransport::Post(std::function<void()> task) {
  if (!task || stopping_.load(std::memory_order_acquire)) return;
  asio::post(io_context_, std::move(task));
}

void SignalingTransport::Connect(
    const std::string& url, const std::string& token,
    signaling::SignalingClient::Handlers handlers,
    ConnectFailure on_failure) {
  Post([this, url, token, handlers = std::move(handlers),
        on_failure = std::move(on_failure)]() mutable {
    if (!client_) {
      if (on_failure) on_failure(500, "信令线程尚未初始化");
      return;
    }
    if (!client_->Connect(url, token, std::move(handlers))) {
      if (on_failure) on_failure(400, "信令地址无效");
    }
  });
}

void SignalingTransport::Request(const std::string& method, const Json& data,
                                 RequestCallback callback) {
  Post([this, method, data, callback = std::move(callback)]() mutable {
    if (!client_) {
      if (callback) callback(signaling::Failure("", 500, "信令客户端未初始化"));
      return;
    }
    client_->Request(method, data, std::move(callback));
  });
}

void SignalingTransport::Notify(const std::string& method, const Json& data) {
  Post([this, method, data] {
    if (client_) client_->Notify(method, data);
  });
}

void SignalingTransport::Send(const Json& message) {
  Post([this, message] {
    if (client_) client_->Send(message);
  });
}

void SignalingTransport::Abort(int code, const std::string& message) {
  Post([this, code, message] {
    if (client_) client_->Abort(code, message);
  });
}

void SignalingTransport::Dispose() {
  Post([this] {
    if (client_) client_->Dispose();
  });
}

void SignalingTransport::Stop(BeforeStop before_stop) {
  if (stopping_.exchange(true, std::memory_order_acq_rel)) return;
  if (!thread_.joinable()) return;

  auto completed = std::make_shared<std::promise<void>>();
  auto future = completed->get_future();
  asio::post(io_context_,
             [this, before_stop = std::move(before_stop), completed]() mutable {
               if (before_stop) before_stop();
               if (client_) client_->Dispose();
               completed->set_value();
             });
  future.wait_for(std::chrono::seconds(2));
  work_guard_.reset();
  io_context_.stop();
  thread_.join();
  client_.reset();
}

}  // namespace rtcwin::engine
