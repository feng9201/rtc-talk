#include "test.h"
#include "tls_fixture.h"
#include "rtc_client/signaling/transport.h"
#include <asio/steady_timer.hpp>
#include <websocketpp/server.hpp>
#include <websocketpp/config/asio.hpp>
#include <websocketpp/config/asio_no_tls.hpp>
#include <type_traits>
#ifdef RTC_TEST_WITH_WEBRTC
#include "rtc_client/media/audio.h"
#endif
using namespace rtc_client;
using namespace std::chrono_literals;
using PlainServer = websocketpp::server<websocketpp::config::asio>;
using TlsServer = websocketpp::server<websocketpp::config::asio_tls>;

template<class Server>
void exercise(TlsFixture& fixture, bool trusted, bool expired, bool ip_host,
              bool protocol_ok = true, bool auth_ok = true) {
  asio::io_context io;
  Server server;
  server.clear_access_channels(websocketpp::log::alevel::all);
  server.clear_error_channels(websocketpp::log::elevel::all);
  server.init_asio(&io);
  if constexpr (std::is_same_v<Server,TlsServer>) {
    auto ctx = fixture.server_context(expired);
    server.set_tls_init_handler([ctx](auto) { return ctx; });
  }
  bool header_ok = false;
  server.set_validate_handler([&](auto handle) {
    auto c = server.get_con_from_hdl(handle);
    header_ok = c->get_request_header("Authorization") == "Bearer test-token" && c->get_resource() == "/talk/v1/ws";
    if (!auth_ok) { c->set_status(websocketpp::http::status_code::unauthorized); return false; }
    if (protocol_ok) c->select_subprotocol("talk.v1.json");
    return true;
  });
  server.set_message_handler([&](auto handle,auto message) {
    if (message->get_payload() == "ping") server.send(handle,"pong",websocketpp::frame::opcode::text);
  });
  server.listen(asio::ip::tcp::endpoint(asio::ip::address_v4::loopback(),0));
  websocketpp::lib::error_code ec;
  const auto port = server.get_local_endpoint(ec).port();
  CHECK(!ec && port != 0);
  server.start_accept();
  WsTransport transport(io);
  asio::steady_timer watchdog(io);
  watchdog.expires_after(5s);
  bool timed_out = false, got_message = false;
  int opened = 0, closed = 0;
  TransportClose last;
  watchdog.async_wait([&](const asio::error_code& error) { if (!error) { timed_out = true; io.stop(); } });
  TransportOptions options;
  constexpr bool tls = std::is_same_v<Server,TlsServer>;
  options.url = std::string(tls ? "wss://" : "ws://") + (ip_host ? "127.0.0.1:" : "localhost:") +
                std::to_string(port) + "/talk/v1/ws";
  options.development_allow_ws = !tls;
  if (trusted) options.ca_file = fixture.ca_file();
  CHECK(transport.connect(options,"test-token",{
    [&] { ++opened; if (!transport.send("ping")) transport.close(); },
    [&](std::string message) { got_message = message == "pong"; transport.close(); },
    [&](TransportClose result) { ++closed; last = result; watchdog.cancel(); server.stop_listening(); io.stop(); }
  }));
  io.run();
  CHECK(!timed_out && closed == 1);
  const bool tls_ok = !tls || (trusted && !expired && !ip_host);
  std::cerr << "case tls=" << tls << " trusted=" << trusted << " expired=" << expired
            << " ip=" << ip_host << " protocol=" << protocol_ok << " auth=" << auth_ok
            << " header=" << header_ok << " opened=" << opened << " message=" << got_message
            << " error=" << static_cast<int>(last.error) << " http=" << last.http_status << '\n';
  if (tls_ok && protocol_ok && auth_ok) {
    CHECK(header_ok && opened == 1 && got_message);
    CHECK(last.error == TransportError::closed && last.close_code == 1000);
  } else {
    CHECK(opened == 0 && !got_message);
    if (!tls_ok) CHECK(last.error == TransportError::tls);
    else if (!auth_ok) CHECK(last.error == TransportError::unauthorized && last.http_status == 401);
    else CHECK(last.error == TransportError::protocol);
  }
}

int main() {
  return test_main([] {
    TransportOptions options;
    options.url = "wss://example.test/talk/v1/ws";
    CHECK(WsTransport::valid_options(options));
    options.url = "ws://example.test/talk/v1/ws";
    CHECK(!WsTransport::valid_options(options));
    options.development_allow_ws = true;
    CHECK(WsTransport::valid_options(options));
    for (const auto* url : {"wss://user:pass@example.test/talk/v1/ws", "wss://example.test/talk/v1/ws?token=secret",
      "wss://example.test/talk/v1/ws#x", "https://example.test/talk/v1/ws", "wss://example.test/other",
      "wss://example.test:0/talk/v1/ws", "wss://example.test/talk/v1/ws\r\nAuthorization:secret"}) {
      options.url = url;
      CHECK(!WsTransport::valid_options(options));
    }
    TlsFixture fixture;
#ifdef RTC_TEST_WITH_WEBRTC
    media::AudioOptions audio_options;audio_options.backend="fake";
    auto audio=media::make_webrtc_audio(audio_options,protocol::Json::array(),
        std::make_shared<MicrophoneGate>(),[](std::string,protocol::Json){});
    CHECK(audio->offer().contains("sdp"));
#endif
    exercise<PlainServer>(fixture,false,false,false);
    exercise<PlainServer>(fixture,false,false,false,false);
    exercise<PlainServer>(fixture,false,false,false,true,false);
    exercise<TlsServer>(fixture,true,false,false);
    exercise<TlsServer>(fixture,false,false,false);
    exercise<TlsServer>(fixture,true,true,false);
    exercise<TlsServer>(fixture,true,false,true);
    // TCP accepted but no WS handshake response: connect deadline is enforced.
    asio::io_context io;
    asio::ip::tcp::acceptor acceptor(io,{asio::ip::address_v4::loopback(),0});
    asio::ip::tcp::socket socket(io);
    acceptor.async_accept(socket,[](const asio::error_code&) {});
    WsTransport transport(io);
    TransportOptions hanging;
    hanging.development_allow_ws = true;
    hanging.connect_timeout = 150ms;
    hanging.url = "ws://127.0.0.1:" + std::to_string(acceptor.local_endpoint().port()) + "/talk/v1/ws";
    bool timeout = false;
    CHECK(transport.connect(hanging,"test-token",{{},{},[&](TransportClose close) {
      timeout = close.error == TransportError::timeout;
      acceptor.close(); socket.close(); io.stop();
    }}));
    io.run();
    CHECK(timeout);
  });
}
