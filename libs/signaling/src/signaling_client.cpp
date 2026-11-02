// File: core/signaling/signaling_client.cpp
// Brief: 实现 WebSocket 连接、请求超时与应答分发。
#include "rtcwin/signaling/signaling_client.h"

#include <windows.h>
#include <winhttp.h>

#include <atomic>
#include <chrono>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

#include "log_compat.h"
#include "rtcwin/signaling/protocol.h"

namespace rtcwin::signaling {
namespace {

using namespace std::chrono_literals;

// 与旧实现保持一致：避免服务端异常时待应答请求无限增长。
constexpr size_t kMaxPendingRequests = 128;
constexpr int kHandshakeTimeoutMs = 8000;
constexpr size_t kSendBufferLimit = 2 * kMaxMessage;

std::wstring Utf8ToWide(const std::string& value) {
  if (value.empty()) return {};
  const int size = MultiByteToWideChar(CP_UTF8, 0, value.data(),
                                        static_cast<int>(value.size()), nullptr,
                                        0);
  if (size <= 0) return {};
  std::wstring result(static_cast<std::size_t>(size), L'\0');
  MultiByteToWideChar(CP_UTF8, 0, value.data(),
                      static_cast<int>(value.size()), result.data(), size);
  return result;
}

std::string WideToUtf8(const std::wstring& value) {
  if (value.empty()) return {};
  const int size = WideCharToMultiByte(CP_UTF8, 0, value.data(),
                                       static_cast<int>(value.size()), nullptr,
                                       0, nullptr, nullptr);
  if (size <= 0) return {};
  std::string result(static_cast<std::size_t>(size), '\0');
  WideCharToMultiByte(CP_UTF8, 0, value.data(),
                      static_cast<int>(value.size()), result.data(), size,
                      nullptr, nullptr);
  return result;
}

}  // namespace

class SignalingClient::WinHttpTransport final
    : public std::enable_shared_from_this<SignalingClient::WinHttpTransport> {
 public:
  using OpenCallback = std::function<void()>;
  using MessageCallback = std::function<void(std::string)>;
  using ClosedCallback = std::function<void(CloseInfo)>;

  WinHttpTransport(asio::io_context& io, OpenCallback on_open,
                   MessageCallback on_message, ClosedCallback on_closed)
      : io_(io),
        on_open_(std::move(on_open)),
        on_message_(std::move(on_message)),
        on_closed_(std::move(on_closed)) {}

  ~WinHttpTransport() { Stop(); }

  bool Start(const std::string& url, const std::string& token,
             std::string* error) {
    if (running_.exchange(true)) {
      if (error) *error = "WinHTTP transport is already running";
      return false;
    }
    try {
      thread_ = std::thread(
          [self = shared_from_this(), url, token] { self->Run(url, token); });
    } catch (const std::exception& exception) {
      running_.store(false);
      if (error) *error = exception.what();
      return false;
    }
    return true;
  }

  bool Send(const std::string& text) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!running_ || websocket_ == nullptr) return false;
    const DWORD result = WinHttpWebSocketSend(
        websocket_, WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE,
        const_cast<char*>(text.data()), static_cast<DWORD>(text.size()));
    if (result != ERROR_SUCCESS) {
      TALK_LOG_WARN("signaling", "WinHTTP WebSocket 发送失败 error={}",
                    result);
      return false;
    }
    return true;
  }

  void Stop() {
    if (!running_.exchange(false) && !thread_.joinable()) return;
    CloseHandles();
    if (thread_.joinable()) thread_.join();
  }

 private:
  void Run(const std::string& url, const std::string& token) {
    if (!running_) return;
    const std::wstring wide_url = Utf8ToWide(url);
    if (wide_url.empty()) {
      Fail("invalid WebSocket URL");
      return;
    }
    std::vector<wchar_t> mutable_url(wide_url.begin(), wide_url.end());
    mutable_url.push_back(L'\0');
    URL_COMPONENTS components{};
    components.dwStructSize = sizeof(components);
    components.dwHostNameLength = static_cast<DWORD>(-1);
    components.dwUrlPathLength = static_cast<DWORD>(-1);
    components.dwExtraInfoLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(mutable_url.data(), 0, 0, &components)) {
      Fail("WinHttpCrackUrl failed");
      return;
    }
    const std::wstring host(components.lpszHostName,
                            components.dwHostNameLength);
    std::wstring path(components.lpszUrlPath, components.dwUrlPathLength);
    if (components.dwExtraInfoLength > 0) {
      path.append(components.lpszExtraInfo, components.dwExtraInfoLength);
    }
    if (path.empty()) path = L"/";
    const INTERNET_PORT port = components.nPort;

    HINTERNET session = WinHttpOpen(
        L"rtc-talk-win/0.1", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (session == nullptr) {
      Fail("WinHttpOpen failed");
      return;
    }
    WinHttpSetTimeouts(session, 8000, 8000, 8000, 8000);
    {
      std::lock_guard<std::mutex> lock(mutex_);
      session_ = session;
    }
    if (!running_) return;

    HINTERNET connection =
        WinHttpConnect(session, host.c_str(), port, 0);
    if (connection == nullptr) {
      Fail("WinHttpConnect failed");
      return;
    }
    {
      std::lock_guard<std::mutex> lock(mutex_);
      connection_ = connection;
    }

    HINTERNET request = WinHttpOpenRequest(
        connection, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    if (request == nullptr) {
      Fail("WinHttpOpenRequest failed");
      return;
    }
    {
      std::lock_guard<std::mutex> lock(mutex_);
      request_ = request;
    }

    DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
    WinHttpSetOption(request, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols,
                     sizeof(protocols));
    BOOL upgrade = TRUE;
    if (!WinHttpSetOption(request, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET,
                          &upgrade, sizeof(upgrade))) {
      Fail("WinHttpSetOption(WEB_SOCKET) failed");
      return;
    }
    const std::wstring authorization =
        L"Authorization: Bearer " + Utf8ToWide(token);
    const std::wstring subprotocol =
        L"Sec-WebSocket-Protocol: talk.v1.json";
    if (!WinHttpAddRequestHeaders(
            request, authorization.c_str(), static_cast<DWORD>(-1),
            WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE) ||
        !WinHttpAddRequestHeaders(
            request, subprotocol.c_str(), static_cast<DWORD>(-1),
            WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE)) {
      Fail("WinHttpAddRequestHeaders failed");
      return;
    }
    if (!WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(request, nullptr)) {
      Fail("WinHTTP WebSocket handshake failed");
      return;
    }
    DWORD status = 0;
    DWORD status_size = sizeof(status);
    if (!WinHttpQueryHeaders(
            request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX, &status, &status_size,
            WINHTTP_NO_HEADER_INDEX) ||
        status != 101) {
      Fail("WinHTTP WebSocket handshake rejected", status);
      return;
    }
    HINTERNET websocket =
        WinHttpWebSocketCompleteUpgrade(request, 0);
    if (websocket == nullptr) {
      Fail("WinHttpWebSocketCompleteUpgrade failed");
      return;
    }
    {
      std::lock_guard<std::mutex> lock(mutex_);
      websocket_ = websocket;
      if (request_) {
        WinHttpCloseHandle(request_);
        request_ = nullptr;
      }
    }
    if (!running_) return;
    asio::post(io_, [self = shared_from_this()] {
      if (self->running_ && self->on_open_) self->on_open_();
    });

    std::vector<char> buffer(64 * 1024);
    std::string pending;
    while (running_) {
      DWORD received = 0;
      WINHTTP_WEB_SOCKET_BUFFER_TYPE type =
          WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE;
      const DWORD result = WinHttpWebSocketReceive(
          websocket, buffer.data(), static_cast<DWORD>(buffer.size()),
          &received, &type);
      if (!running_ || result != ERROR_SUCCESS ||
          type == WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE) {
        break;
      }
      if (type == WINHTTP_WEB_SOCKET_UTF8_FRAGMENT_BUFFER_TYPE ||
          type == WINHTTP_WEB_SOCKET_BINARY_FRAGMENT_BUFFER_TYPE) {
        pending.append(buffer.data(), received);
      } else if (type == WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE ||
                 type == WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE) {
        pending.append(buffer.data(), received);
        asio::post(io_, [self = shared_from_this(),
                         message = std::move(pending)]() mutable {
          if (self->running_ && self->on_message_) {
            self->on_message_(std::move(message));
          }
        });
        pending.clear();
      }
      if (pending.size() > kMaxMessage) {
        TALK_LOG_WARN("signaling", "WinHTTP 消息超过上限 size={}",
                      pending.size());
        break;
      }
    }
    CloseHandles();
    if (running_.exchange(false) && on_closed_) {
      asio::post(io_, [self = shared_from_this()] {
        if (self->on_closed_) self->on_closed_(CloseInfo{});
      });
    }
  }

  void Fail(const char* reason, DWORD status = 0) {
    TALK_LOG_WARN("signaling", "{} status={} winhttp_error={}", reason, status, GetLastError());
    CloseHandles();
    if (running_.exchange(false) && on_closed_) {
      asio::post(io_, [self = shared_from_this(), status] {
        CloseInfo info;
        info.handshake_failed = true;
        info.status = static_cast<int>(status);
        info.fatal =
            status == 401 || status == 403 || status == 404;
        if (self->on_closed_) self->on_closed_(info);
      });
    }
  }

  void CloseHandles() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (websocket_) {
      WinHttpCloseHandle(websocket_);
      websocket_ = nullptr;
    }
    if (request_) {
      WinHttpCloseHandle(request_);
      request_ = nullptr;
    }
    if (connection_) {
      WinHttpCloseHandle(connection_);
      connection_ = nullptr;
    }
    if (session_) {
      WinHttpCloseHandle(session_);
      session_ = nullptr;
    }
  }

  asio::io_context& io_;
  OpenCallback on_open_;
  MessageCallback on_message_;
  ClosedCallback on_closed_;
  std::atomic<bool> running_{false};
  std::mutex mutex_;
  HINTERNET session_ = nullptr;
  HINTERNET connection_ = nullptr;
  HINTERNET request_ = nullptr;
  HINTERNET websocket_ = nullptr;
  std::thread thread_;
};

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
  const bool tls = url.rfind("wss://", 0) == 0;
  const bool plain = url.rfind("ws://", 0) == 0;
  if (!tls && !plain) {
    TALK_LOG_ERROR("signaling", "不支持的 WebSocket 地址 url={}", url);
    return false;
  }
  tls_ = tls;
  TALK_LOG_INFO("signaling", "连接 url={} generation={}", url,
                generation);

  if (tls) {
    winhttp_transport_ = std::make_shared<WinHttpTransport>(
        io_,
        [this, generation] {
          if (disposed_ || generation != generation_) return;
          open_ = true;
          TALK_LOG_INFO("signaling", "WSS 已打开 generation={}",
                        generation);
          if (handlers_.on_open) handlers_.on_open();
        },
        [this, generation](std::string payload) {
          if (disposed_ || generation != generation_) return;
          try {
            HandleMessage(ParseMessage(payload));
          } catch (const std::exception&) {
            TALK_LOG_WARN("signaling", "丢弃无效 WSS 信令 size={} error={}",
                          payload.size(), "invalid payload");
            if (handlers_.on_error) handlers_.on_error(400, "收到无效信令");
          } catch (...) {
            TALK_LOG_WARN("signaling", "丢弃无效 WSS 信令 size={}",
                          payload.size());
            if (handlers_.on_error) handlers_.on_error(400, "收到无效信令");
          }
        },
        [this, generation](CloseInfo info) {
          if (disposed_ || generation != generation_) return;
          open_ = false;
          ++generation_;
          TALK_LOG_INFO("signaling", "WSS 关闭 code={} status={} fatal={}",
                        info.code, info.status, info.fatal);
          if (handlers_.on_closed) handlers_.on_closed(info);
        });
    std::string transport_error;
    if (!winhttp_transport_->Start(url, token, &transport_error)) {
      TALK_LOG_ERROR("signaling", "WSS 启动失败 error={}", transport_error);
      winhttp_transport_.reset();
      return false;
    }
    return true;
  }

  const auto configure = [this, generation, token](auto connection) {
    connection->add_subprotocol("talk.v1.json");
    connection->append_header("Authorization", "Bearer " + token);
    connection->set_open_handshake_timeout(kHandshakeTimeoutMs);
    connection->set_open_handler([this, generation](auto) {
      if (disposed_ || generation != generation_) return;
      open_ = true;
      TALK_LOG_INFO("signaling", "WebSocket 已打开 generation={}",
                    generation);
      if (handlers_.on_open) handlers_.on_open();
    });
    connection->set_message_handler(
        [this, generation](auto, auto message) {
          if (disposed_ || generation != generation_) return;
          try {
            HandleMessage(ParseMessage(message->get_payload()));
          } catch (const std::exception&) {
            TALK_LOG_WARN("signaling", "丢弃无效信令 size={} error={}",
                          message->get_payload().size(), "invalid payload");
            if (handlers_.on_error) handlers_.on_error(400, "收到无效信令");
          } catch (...) {
            TALK_LOG_WARN("signaling", "丢弃无效信令 size={}",
                          message->get_payload().size());
            if (handlers_.on_error) handlers_.on_error(400, "收到无效信令");
          }
        });
    connection->set_close_handler([this, generation](auto) {
      if (disposed_ || generation != generation_) return;
      CloseInfo info;
      info.code = connection_ ? connection_->get_remote_close_code() : 0;
      info.fatal = info.code == 4001 || info.code == 4003;
      open_ = false;
      ++generation_;
      TALK_LOG_INFO("signaling", "连接关闭 code={} fatal={}", info.code,
                    info.fatal);
      if (handlers_.on_closed) handlers_.on_closed(info);
    });
    connection->set_fail_handler([this, generation](auto) {
      if (disposed_ || generation != generation_) return;
      CloseInfo info;
      info.handshake_failed = true;
      info.status =
          connection_ ? static_cast<int>(connection_->get_response_code()) : 0;
      info.fatal =
          info.status == 401 || info.status == 403 || info.status == 404;
      open_ = false;
      ++generation_;
      TALK_LOG_WARN("signaling", "握手失败 status={} fatal={}", info.status,
                    info.fatal);
      if (handlers_.on_closed) handlers_.on_closed(info);
    });
  };

  websocketpp::lib::error_code error;
  connection_ = ws_.get_connection(url, error);
  if (error) {
    connection_.reset();
    TALK_LOG_ERROR("signaling", "地址无效 url={} error={}", url,
                   error.message());
    return false;
  }
  configure(connection_);
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
  if (!open_ || (tls_ ? !winhttp_transport_ : !connection_)) {
    TALK_LOG_DEBUG("signaling", "发送被跳过：连接未就绪");
    return false;
  }
  const std::string text = message.dump();
  if (text.size() > kMaxMessage) {
    TALK_LOG_WARN("signaling", "发送被拒绝 size={} limit={}", text.size(),
                  kMaxMessage);
    return false;
  }
  if (tls_) {
    if (!winhttp_transport_->Send(text)) {
      TALK_LOG_ERROR("signaling", "WSS 发送失败");
      return false;
    }
    return true;
  }
  const std::size_t buffered = connection_->get_buffered_amount();
  if (text.size() > kMaxMessage ||
      buffered > kSendBufferLimit) {
    TALK_LOG_WARN("signaling", "发送被拒绝 size={} buffered={}",
                  text.size(), buffered);
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
  if (winhttp_transport_) {
    winhttp_transport_->Stop();
    winhttp_transport_.reset();
  }
  if (!connection_) {
    return;
  }
  websocketpp::lib::error_code error;
  connection_->terminate(error);
  connection_.reset();
  TALK_LOG_DEBUG("signaling", "连接已终止 error={}", error.message());
}

}  // namespace rtcwin::signaling
