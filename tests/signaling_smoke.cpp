#include "rtcwin/signaling/signaling_client.h"

#include <asio.hpp>

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <utility>

namespace {

using rtcwin::signaling::Json;
using rtcwin::signaling::SignalingClient;

std::string Arg(int argc, char** argv, const std::string& name) {
  for (int i = 1; i + 1 < argc; ++i) {
    if (argv[i] == name) return argv[i + 1];
  }
  return {};
}

int Fail(const std::string& message) {
  std::cerr << message << '\n';
  return 1;
}

}  // namespace

int main(int argc, char** argv) {
  const std::string url = Arg(argc, argv, "--url");
  std::string token = Arg(argc, argv, "--token");
  if (token.empty()) {
    char* environment_token = nullptr;
    size_t environment_length = 0;
    if (_dupenv_s(&environment_token, &environment_length,
                  "RTCWIN_SIGNALING_TOKEN") == 0 &&
        environment_token != nullptr) {
      token = environment_token;
      free(environment_token);
    }
  }
  const std::string uid = Arg(argc, argv, "--uid");
  const std::string room = Arg(argc, argv, "--room");
  if (url.empty() || token.empty() || uid.empty() || room.empty()) {
    return Fail("usage: rtcwin_signaling_smoke --url <ws> --token <token> "
                "--uid <uid> --room <room>");
  }

  asio::io_context io;
  SignalingClient client(io);
  asio::steady_timer timeout(io);
  timeout.expires_after(std::chrono::seconds(20));
  timeout.async_wait([&](const asio::error_code& error) {
    if (error) return;
    std::cerr << "signaling smoke timeout\n";
    io.stop();
  });

  bool finished = false;
  bool joined = false;

  SignalingClient::Handlers handlers;
  handlers.on_error = [&](int code, std::string message) {
    if (finished) return;
    std::cerr << "signaling error code=" << code << " " << message << '\n';
    io.stop();
  };
  handlers.on_closed = [&](const SignalingClient::CloseInfo& info) {
    if (finished) return;
    std::cerr << "signaling closed handshake=" << info.handshake_failed
              << " status=" << info.status << " code=" << info.code << '\n';
    io.stop();
  };
  handlers.on_open = [&] {
    Json hello = {
        {"client",
         {{"platform", "windows-x64"},
          {"version", "0.1.0"},
          {"sdk", "rtcwin-s2"}}},
        {"caps",
         {{"audio", {{"codecs", Json::array({"opus"})}, {"aec", "webrtc"}}},
          {"p2p", false},
          {"p2p_video", false},
          {"sfu_sdp", "unified-plan"}}}};
    client.Request("session.hello", hello, [&](Json result) {
      if (!result.value("ok", false)) {
        std::cerr << "session.hello failed: " << result.dump() << '\n';
        io.stop();
        return;
      }
      std::cout << "hello ok\n";
      Json join = {
          {"room_id", room},
          {"display", uid},
          {"media",
           {{"audio", {{"send", true}, {"muted", true}}},
            {"video", {{"send", false}}}}},
          {"policy", {{"talk_mode", "free"}, {"media_mode", "sfu"}}}};
      client.Request("room.join", join, [&](Json joined_result) {
        if (!joined_result.value("ok", false)) {
          std::cerr << "room.join failed: " << joined_result.dump() << '\n';
          io.stop();
          return;
        }
        joined = true;
        const Json data = joined_result.value("data", Json::object());
        std::cout << "room.join ok mode="
                  << data.value("media_mode", std::string("unknown")) << '\n';
        client.Request("room.leave", {{"room_id", room}}, [&](Json left) {
          if (!left.value("ok", false)) {
            std::cerr << "room.leave failed: " << left.dump() << '\n';
            io.stop();
            return;
          }
          finished = true;
          std::cout << "room.leave ok\n";
          client.Dispose();
          io.stop();
        });
      });
    });
  };

  if (!client.Connect(url, token, std::move(handlers))) {
    return Fail("invalid signaling URL");
  }
  io.run();
  if (!finished) return Fail(joined ? "smoke ended before leave" :
                                     "smoke ended before join");
  std::cout << "signaling smoke passed\n";
  return 0;
}