#include "rtcwin/signaling/signaling_client.h"

#include <asio.hpp>

#include <chrono>
#include <iostream>
#include <string>

int main(int argc, char** argv) {
  const std::string url =
      argc > 1 ? argv[1] : "wss://localhost:9443/talk/v1/ws";
  asio::io_context io;
  rtcwin::signaling::SignalingClient client(io);
  asio::steady_timer timeout(io);
  timeout.expires_after(std::chrono::seconds(10));
  timeout.async_wait([&](const asio::error_code& error) {
    if (!error) {
      std::cerr << "WSS transport smoke timeout\n";
      io.stop();
    }
  });

  bool opened = false;
  bool closed = false;
  int status = 0;
  rtcwin::signaling::SignalingClient::Handlers handlers;
  handlers.on_open = [&] {
    opened = true;
    timeout.cancel();
    client.Dispose();
    io.stop();
  };
  handlers.on_closed = [&](const rtcwin::signaling::SignalingClient::CloseInfo&
                               info) {
    closed = true;
    status = info.status;
    timeout.cancel();
    io.stop();
  };
  handlers.on_error = [](int code, std::string message) {
    std::cerr << "WSS transport error code=" << code
              << " message=" << message << '\n';
  };
  if (!client.Connect(url, "wss-smoke-token", std::move(handlers))) {
    std::cerr << "WSS transport connect start failed\n";
    return 2;
  }
  io.run();
  if (!opened) {
    std::cerr << "WSS transport did not open closed=" << closed
              << " status=" << status << '\n';
    return 1;
  }
  std::cout << "WSS transport smoke: PASS\n";
  return 0;
}
