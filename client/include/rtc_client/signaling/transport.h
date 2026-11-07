#pragma once
#include <asio/io_context.hpp>
#include <chrono>
#include <functional>
#include <memory>
#include <string>

namespace rtc_client {
struct TransportOptions {
  std::string url;
  bool development_allow_ws{false};
  std::string ca_file;  // Empty => platform trust store. No verify-off option.
  std::chrono::milliseconds connect_timeout{8000};
};
enum class TransportError { closed, configuration, network, tls, protocol, timeout, unauthorized, forbidden };
struct TransportClose {
  TransportError error{TransportError::closed};
  unsigned http_status{}, close_code{};
};
struct TransportCallbacks {
  std::function<void()> opened;
  std::function<void(std::string)> message;
  std::function<void(TransportClose)> closed;
};

// Control-executor confined. The owner MUST keep Transport alive until its
// io_context runner has stopped/joined. Callbacks cannot destroy the transport.
// One stable endpoint instance is reused across connection generations.
class WsTransport final {
 public:
  explicit WsTransport(asio::io_context&);
  ~WsTransport();
  WsTransport(const WsTransport&) = delete;
  WsTransport& operator=(const WsTransport&) = delete;
  static bool valid_options(const TransportOptions&) noexcept;
  bool connect(TransportOptions, std::string bearer_token, TransportCallbacks);
  bool send(const std::string&);
  void close();
  bool is_open() const noexcept;

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};
}  // namespace rtc_client
