// File: core/signaling/signaling_client.cpp
// Brief: 实现 WebSocket 连接、请求超时与应答分发。
#include "signaling/signaling_client.h"

#include <chrono>
#include <exception>
#include <utility>

#include "base/log.h"
#include "signaling/protocol.h"

namespace talk {
namespace {

using namespace std::chrono_literals;

// 与旧实现保持一致：避免服务端异常时待应答请求无限增长。
constexpr size_t kMaxPendingRequests = 128;
constexpr int kHandshakeTimeoutMs = 8000;
constexpr size_t kSendBufferLimit = 2 * kMaxMessage;

}  // namespace

SignalingClient::SignalingClient(asio::io_context& io) : io_(io) {
  ws_.clear_access_channels(websocketpp::log::alevel::all);
  ws_.clear_error_channels(websocketpp::log::elevel::all);
  ws_.init_asio(&io_);
  ws_.set_max_message_size(kMaxMessage);
}

SignalingClient::~SignalingClient() { Dispose(); }

bool SignalingClient::Connect(const std::string& url,
                              const std::string& token, Handlers handlers) {
  Abort(480, "连接已关闭");
  handlers_ = std::move(handlers);
  const uint64_t generation = ++generation_;
  open_ = false;
  TALK_LOG_INFO("signaling", "连接 url={} generation={}", url,
                generation);

  websocketpp::lib::error_code error;
  connection_ = ws_.get_connection(url, error);
  if (error) {
    connection_.reset();
    TALK_LOG_ERROR("signaling", "地址无效 url={} error={}", url,
                   error.message());
    return false;
  }
  connection_->add_subprotocol("talk.v1.json");
  connection_->append_header("Authorization", "Bearer " + token);
  connection_->set_open_handshake_timeout(kHandshakeTimeoutMs);
  connection_->set_open_handler([this, generation](auto) {
    if (disposed_ || generation != generation_) {
      return;
    }
    open_ = true;
    TALK_LOG_INFO("signaling", "WebSocket 已打开 generation={}",
                  generation);
    if (handlers_.on_open) {
      handlers_.on_open();
    }
  });
  connection_->set_message_handler(
      [this, generation](auto, Ws::message_ptr message) {
        if (disposed_ || generation != generation_) {
          return;
        }
        try {
          HandleMessage(ParseMessage(message->get_payload()));
        } catch (const std::exception& error) {
          TALK_LOG_WARN("signaling", "丢弃无效信令 size={} error={}",
                        message->get_payload().size(), error.what());
          if (handlers_.on_error) {
            handlers_.on_error(400, "收到无效信令");
          }
        } catch (...) {
          TALK_LOG_WARN("signaling", "丢弃无效信令 size={}",
                        message->get_payload().size());
          if (handlers_.on_error) {
            handlers_.on_error(400, "收到无效信令");
          }
        }
      });
  connection_->set_close_handler([this, generation](auto) {
    if (disposed_ || generation != generation_) {
      return;
    }
    CloseInfo info;
    info.code = connection_ ? connection_->get_remote_close_code() : 0;
    info.fatal = info.code == 4001 || info.code == 4003;
    open_ = false;
    ++generation_;
    TALK_LOG_INFO("signaling", "连接关闭 code={} fatal={}", info.code,
                  info.fatal);
    if (handlers_.on_closed) {
      handlers_.on_closed(info);
    }
  });
  connection_->set_fail_handler([this, generation](auto) {
    if (disposed_ || generation != generation_) {
      return;
    }
    CloseInfo info;
    info.handshake_failed = true;
    info.status =
        connection_ ? static_cast<int>(connection_->get_response_code()) : 0;
    info.fatal = info.status == 401 || info.status == 403 || info.status == 404;
    open_ = false;
    ++generation_;
    TALK_LOG_WARN("signaling", "握手失败 status={} fatal={}", info.status,
                  info.fatal);
    if (handlers_.on_closed) {
      handlers_.on_closed(info);
    }
  });
  ws_.connect(connection_);
  return true;
}

void SignalingClient::Abort(int code, const std::string& message) {
  ++generation_;
  open_ = false;
  auto pending = std::move(pending_);
  pending_.clear();
  TALK_LOG_INFO("signaling", "中止连接 code={} pending={} msg={}", code,
                pending.size(), message);
  for (auto& item : pending) {
    item.second.timer->cancel();
    if (item.second.done) {
      item.second.done(Failure(item.first, code, message));
    }
  }
  Terminate();
}

void SignalingClient::Dispose() {
  if (disposed_) {
    return;
  }
  disposed_ = true;
  ++generation_;
  open_ = false;
  TALK_LOG_DEBUG("signaling", "释放连接");
  CancelPending();
  Terminate();
}

bool SignalingClient::Send(const Json& message) {
  if (!open_ || !connection_) {
    TALK_LOG_DEBUG("signaling", "发送被跳过：连接未就绪");
    return false;
  }
  const std::string text = message.dump();
  if (text.size() > kMaxMessage ||
      connection_->get_buffered_amount() > kSendBufferLimit) {
    TALK_LOG_WARN("signaling", "发送被拒绝 size={} buffered={}",
                  text.size(), connection_->get_buffered_amount());
    return false;
  }
  const websocketpp::lib::error_code error =
      connection_->send(text, websocketpp::frame::opcode::text);
  if (error) {
    TALK_LOG_ERROR("signaling", "发送失败 error={}", error.message());
    return false;
  }
  return true;
}

void SignalingClient::Request(std::string method, Json data, Done done) {
  if (!open_ || pending_.size() >= kMaxPendingRequests) {
    TALK_LOG_WARN("signaling", "请求被拒绝 method={} open={} pending={}",
                  method, open_, pending_.size());
    if (done) {
      done(Failure("", 480, "连接不可用"));
    }
    return;
  }
  const std::string id = "c-" + std::to_string(++next_request_);
  auto timer = std::make_shared<asio::steady_timer>(io_);
  // SFU 协商涉及服务端建 transport，给更长的等待时间。
  timer->expires_after(method.rfind("sfu.", 0) == 0 ? 15s : 10s);
  pending_.emplace(id, Pending{timer, std::move(done)});
  timer->async_wait([this, id, method](const asio::error_code& error) {
    auto it = pending_.find(id);
    if (error || it == pending_.end()) {
      return;
    }
    Done callback = std::move(it->second.done);
    pending_.erase(it);
    TALK_LOG_WARN("signaling", "请求超时 method={} id={}", method, id);
    if (callback) {
      callback(Failure(id, 504, "请求超时"));
    }
  });
  if (!Send(MakeRequest(id, method, data))) {
    timer->cancel();
    auto it = pending_.find(id);
    if (it == pending_.end()) {
      return;
    }
    Done callback = std::move(it->second.done);
    pending_.erase(it);
    TALK_LOG_ERROR("signaling", "请求发送失败 method={} id={}", method,
                   id);
    if (callback) {
      callback(Failure(id, 480, "发送失败"));
    }
  }
}

void SignalingClient::Notify(const std::string& method, Json data) {
  Send(MakeNotification(method, std::move(data)));
}

void SignalingClient::HandleMessage(Json message) {
  const std::string type = message["type"].get<std::string>();
  if (type == "res") {
    Resolve(std::move(message));
    return;
  }
  if (handlers_.on_message) {
    handlers_.on_message(std::move(message));
  }
}

void SignalingClient::Resolve(Json message) {
  const std::string id = message["id"].get<std::string>();
  auto it = pending_.find(id);
  if (it == pending_.end()) {
    TALK_LOG_DEBUG("signaling", "忽略迟到响应 id={}", id);
    return;
  }
  it->second.timer->cancel();
  Done callback = std::move(it->second.done);
  pending_.erase(it);
  if (callback) {
    callback(std::move(message));
  }
}

void SignalingClient::CancelPending() {
  auto pending = std::move(pending_);
  pending_.clear();
  if (!pending.empty()) {
    TALK_LOG_DEBUG("signaling", "取消待应答请求 pending={}",
                   pending.size());
  }
  for (auto& item : pending) {
    item.second.timer->cancel();
  }
}

void SignalingClient::Terminate() {
  if (!connection_) {
    return;
  }
  websocketpp::lib::error_code error;
  connection_->terminate(error);
  connection_.reset();
  TALK_LOG_DEBUG("signaling", "连接已终止 error={}", error.message());
}

}  // namespace talk
