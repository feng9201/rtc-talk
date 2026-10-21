// File: core/signaling/signaling_client.h
// Brief: WebSocket 信令通道：握手、请求应答配对、超时与连接关闭。
#ifndef TALK_SIGNALING_SIGNALING_CLIENT_H_
#define TALK_SIGNALING_SIGNALING_CLIENT_H_

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <string>

#include <asio.hpp>
#include <websocketpp/client.hpp>
#include <websocketpp/config/asio_no_tls_client.hpp>

#include "base/json.h"

namespace talk {

// 只在核心控制线程调用；所有回调也在该线程执行。
class SignalingClient {
 public:
  using Done = std::function<void(Json)>;

  // 连接关闭原因；fatal 表示不应自动重连。
  struct CloseInfo {
    bool handshake_failed = false;
    bool fatal = false;
    int status = 0;
    int code = 0;
  };

  // 连接事件回调；由会话层在 Connect 时注入。
  struct Handlers {
    std::function<void()> on_open;
    std::function<void(Json)> on_message;
    std::function<void(const CloseInfo&)> on_closed;
    std::function<void(int, std::string)> on_error;
  };

  explicit SignalingClient(asio::io_context& io);
  ~SignalingClient();

  SignalingClient(const SignalingClient&) = delete;
  SignalingClient& operator=(const SignalingClient&) = delete;

  // 发起连接；地址无效时返回 false，不触发任何回调。
  bool Connect(const std::string& url, const std::string& token,
               Handlers handlers);

  // 终止连接并以指定错误结束所有待应答请求。
  void Abort(int code, const std::string& message);

  // 析构前的静默清理：不回调，不抛异常。
  void Dispose();

  bool Send(const Json& message);
  void Request(std::string method, Json data, Done done);
  void Notify(const std::string& method, Json data);

  bool open() const { return open_; }

 private:
  using Ws = websocketpp::client<websocketpp::config::asio_client>;
  struct Pending {
    std::shared_ptr<asio::steady_timer> timer;
    Done done;
  };

  void HandleMessage(Json message);
  void Resolve(Json message);
  void CancelPending();
  void Terminate();

  asio::io_context& io_;
  Ws ws_;
  Ws::connection_ptr connection_;
  Handlers handlers_;
  std::map<std::string, Pending> pending_;
  uint64_t generation_ = 0;
  uint64_t next_request_ = 0;
  bool open_ = false;
  bool disposed_ = false;
};

}  // namespace talk

#endif  // TALK_SIGNALING_SIGNALING_CLIENT_H_
