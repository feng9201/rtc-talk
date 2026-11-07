#include "rtc_client/signaling/transport.h"
#include "rtc_client/protocol/message.h"
#include "rtc_client/logging/logger.h"
#include <asio/ssl.hpp>
#include <asio/steady_timer.hpp>
#include <websocketpp/client.hpp>
#include <websocketpp/config/asio_client.hpp>
#include <websocketpp/config/asio_no_tls_client.hpp>
#include <websocketpp/uri.hpp>
#include <limits>

namespace rtc_client {
namespace {
using Plain = websocketpp::client<websocketpp::config::asio_client>;
using Secure = websocketpp::client<websocketpp::config::asio_tls_client>;
bool valid_token(const std::string& token) {
  if (token.empty() || token.size() > 8192) return false;
  for (unsigned char c : token) if (c < 0x21 || c > 0x7e) return false;
  return true;
}
}  // namespace

class WsTransport::Impl {
 public:
  explicit Impl(asio::io_context& io) : deadline_(io) {
    initialize(plain_,io);
    initialize(secure_,io);
  }
  ~Impl() {
    active_ = false;
    deadline_.cancel();
    abort_socket();
  }
  template<class Client> void initialize(Client& client, asio::io_context& io) {
    client.clear_access_channels(websocketpp::log::alevel::all);
    client.clear_error_channels(websocketpp::log::elevel::all);
    client.init_asio(&io);
    client.set_max_message_size(protocol::max_message_bytes);
  }
  template<class Connection> void abort(Connection& connection) noexcept {
    if (connection) {
      try { connection->terminate(websocketpp::lib::error_code{}); } catch (...) {}
    }
  }
  void abort_socket() noexcept { abort(plain_connection_); abort(secure_connection_); }
  void finish(std::uint64_t serial, TransportClose close) {
    if (!active_ || serial != serial_) return;
    active_ = opened_ = false;
    deadline_.cancel();
    abort_socket();
    logging::emit(logging::Level::info,"transport.closed",{{"http_status",close.http_status},{"close_code",close.close_code}});
    auto callback = callbacks_.closed;
    try { if (callback) callback(close); } catch (...) {}
  }
  template<class Connection> void configure(Connection connection, std::uint64_t serial) {
    const std::weak_ptr<typename Connection::element_type> weak = connection;
    connection->add_subprotocol("talk.v1.json");
    connection->set_open_handshake_timeout(timeout_ms_);
    connection->set_close_handshake_timeout(2000);
    connection->set_open_handler([this,weak,serial](auto) {
      auto connection = weak.lock();
      if (!connection) return;
      if (!active_ || serial != serial_) return;
      // websocketpp 0.8.2 does not populate the client's m_subprotocol even
      // though its getter documentation says it does. Validate the actual
      // server response header; never disable subprotocol verification.
      if (connection->get_response_header("Sec-WebSocket-Protocol") != "talk.v1.json") {
        finish(serial,{TransportError::protocol,0,0}); return;
      }
      opened_ = true;
      deadline_.cancel();
      logging::emit(logging::Level::info,"transport.opened");
      try { if (callbacks_.opened) callbacks_.opened(); }
      catch (...) { finish(serial,{TransportError::protocol,0,0}); }
    });
    connection->set_message_handler([this,serial](auto,auto message) {
      if (!active_ || !opened_ || serial != serial_) return;
      if (message->get_opcode() != websocketpp::frame::opcode::text ||
          message->get_payload().size() > protocol::max_message_bytes) {
        finish(serial,{TransportError::protocol,0,0}); return;
      }
      try { if (callbacks_.message) callbacks_.message(message->get_payload()); }
      catch (...) { finish(serial,{TransportError::protocol,0,0}); }
    });
    connection->set_fail_handler([this,weak,serial](auto) {
      auto connection = weak.lock();
      if (!connection) return;
      const auto status = static_cast<unsigned>(connection->get_response_code());
      auto error = secure_active_ ? TransportError::tls : TransportError::network;
      if (status == 401) error = TransportError::unauthorized;
      else if (status == 403) error = TransportError::forbidden;
      finish(serial,{error,status,0});
    });
    connection->set_close_handler([this,weak,serial](auto) {
      auto connection = weak.lock();
      if (!connection) return;
      finish(serial,{TransportError::closed,0,static_cast<unsigned>(connection->get_remote_close_code())});
    });
  }
  bool connect(TransportOptions options, std::string token, TransportCallbacks callbacks) {
    if (active_ || !WsTransport::valid_options(options) || !valid_token(token) ||
        serial_ == std::numeric_limits<std::uint64_t>::max()) return false;
    abort_socket();
    plain_connection_.reset(); secure_connection_.reset();
    callbacks_ = std::move(callbacks);
    const auto serial = ++serial_;
    secure_active_ = options.url.rfind("wss://",0) == 0;
    timeout_ms_ = static_cast<long>(options.connect_timeout.count());
    websocketpp::lib::error_code ec;
    try {
      if (secure_active_) {
        const auto uri = websocketpp::uri(options.url);
        const auto host = uri.get_host();
        secure_.set_tls_init_handler([host,ca=options.ca_file](auto) {
          auto ctx = std::make_shared<asio::ssl::context>(asio::ssl::context::tls_client);
          if (SSL_CTX_set_min_proto_version(ctx->native_handle(),TLS1_2_VERSION) != 1)
            throw std::runtime_error("TLS configuration failed");
          ctx->set_verify_mode(asio::ssl::verify_peer);
          if (ca.empty()) ctx->set_default_verify_paths();
          else ctx->load_verify_file(ca);
          ctx->set_verify_callback(asio::ssl::host_name_verification(host));
          return ctx;
        });
        secure_connection_ = secure_.get_connection(options.url,ec);
        if (ec || !secure_connection_) return false;
        configure(secure_connection_,serial);
        secure_connection_->append_header("Authorization","Bearer " + token);
      } else {
        plain_connection_ = plain_.get_connection(options.url,ec);
        if (ec || !plain_connection_) return false;
        configure(plain_connection_,serial);
        plain_connection_->append_header("Authorization","Bearer " + token);
      }
      active_ = true;
      deadline_.expires_after(options.connect_timeout);
      deadline_.async_wait([this,serial](const asio::error_code& error) {
        if (!error) finish(serial,{TransportError::timeout,0,0});
      });
      if (secure_active_) secure_.connect(secure_connection_);
      else plain_.connect(plain_connection_);
      return true;
    } catch (...) {
      // The caller receives a fixed configuration failure, never an exception
      // containing Authorization, a URL query or a TLS library diagnostic.
      active_ = opened_ = false;
      deadline_.cancel();
      abort_socket();
      return false;
    }
  }
  template<class Connection> bool send_with(Connection connection, const std::string& text) {
    if (!connection || connection->get_buffered_amount() + text.size() > 2 * protocol::max_message_bytes)
      return false;
    return !connection->send(text,websocketpp::frame::opcode::text);
  }
  bool send(const std::string& text) {
    if (!opened_ || text.empty() || text.size() > protocol::max_message_bytes) return false;
    try { return secure_active_ ? send_with(secure_connection_,text) : send_with(plain_connection_,text); }
    catch (...) { return false; }
  }
  void close() {
    if (!active_) return;
    if (!opened_) { finish(serial_,{TransportError::closed,0,1000}); return; }
    opened_ = false;
    websocketpp::lib::error_code error;
    if (secure_active_) secure_connection_->close(websocketpp::close::status::normal,"client shutdown",error);
    else plain_connection_->close(websocketpp::close::status::normal,"client shutdown",error);
    if (error) { finish(serial_,{TransportError::network,0,0}); return; }
    const auto serial = serial_;
    deadline_.expires_after(std::chrono::seconds(2));
    deadline_.async_wait([this,serial](const asio::error_code& ec) {
      if (!ec) finish(serial,{TransportError::timeout,0,0});
    });
  }
  Plain plain_;
  Secure secure_;
  Plain::connection_ptr plain_connection_;
  Secure::connection_ptr secure_connection_;
  asio::steady_timer deadline_;
  TransportCallbacks callbacks_;
  std::uint64_t serial_{};
  long timeout_ms_{8000};
  bool active_{false}, opened_{false}, secure_active_{false};
};

WsTransport::WsTransport(asio::io_context& io) : impl_(std::make_unique<Impl>(io)) {}
WsTransport::~WsTransport() = default;
bool WsTransport::valid_options(const TransportOptions& options) noexcept {
  try {
    if (options.url.empty() || options.url.size() > 2048 ||
        options.connect_timeout.count() <= 0 || options.connect_timeout > std::chrono::seconds(30)) return false;
    for (unsigned char c : options.url) if (c <= 0x20 || c >= 0x7f || c == '\\' || c == '@' || c == '?' || c == '#')
      return false;
    websocketpp::uri uri(options.url);
    if (!uri.get_valid() || uri.get_host().empty() || uri.get_port() == 0 || uri.get_resource() != "/talk/v1/ws")
      return false;
    return uri.get_scheme() == "wss" || (uri.get_scheme() == "ws" && options.development_allow_ws);
  } catch (...) { return false; }
}
bool WsTransport::connect(TransportOptions options, std::string token, TransportCallbacks callbacks) {
  return impl_->connect(std::move(options),std::move(token),std::move(callbacks));
}
bool WsTransport::send(const std::string& text) { return impl_->send(text); }
void WsTransport::close() { impl_->close(); }
bool WsTransport::is_open() const noexcept { return impl_->opened_; }
}  // namespace rtc_client
