#include "rtcwin/client_api.h"
#include "rtcwin/signaling/protocol.h"
#include "rtcwin/webrtc_media/media_session.h"

#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>

#include <chrono>
#include <atomic>
#include <future>
#include <iostream>
#include <memory>
#include <thread>

int main() {
  using namespace rtcwin::signaling;
  using Server = websocketpp::server<websocketpp::config::asio>;
  using namespace std::chrono_literals;

  rtcwin::webrtc_media::MediaSession publisher;
  rtcwin::webrtc_media::MediaConfig config;
  config.audio_backend = "fake";
  rtcwin::webrtc_media::MediaSfuOffer offer;
  rtcwin::webrtc_media::MediaError error;
  if (!publisher.Initialize(config, {}, {}, &error) ||
      !publisher.CreateSfuPublishOffer(&offer, &error) ||
      offer.camera_mid.empty() || offer.screen_mid.empty()) {
    std::cerr << "Could not create test offer: " << error.message << '\n';
    return 1;
  }

  const Json subscription = {
      {"sdp", offer.sdp},
      {"consumers", Json::array({
          {{"mid", offer.screen_mid}, {"uid", "A"}, {"kind", "video"},
           {"source", "screen"}},
          {{"mid", offer.camera_mid}, {"uid", "B"}, {"kind", "video"},
           {"source", "camera"}}})}};
  Server server;
  server.clear_access_channels(websocketpp::log::alevel::all);
  server.init_asio();
  server.set_validate_handler([&](websocketpp::connection_hdl handle) {
    server.get_con_from_hdl(handle)->select_subprotocol("talk.v1.json");
    return true;
  });
  server.listen(asio::ip::tcp::endpoint(asio::ip::address_v4::loopback(), 0));
  websocketpp::lib::error_code endpoint_error;
  const auto port = server.get_local_endpoint(endpoint_error).port();
  if (endpoint_error) return 1;
  std::promise<bool> result;
  auto completed = result.get_future();
  bool join_sent = false;
  bool finished = false;
  const auto send = [&](websocketpp::connection_hdl handle, const Json& message) {
    server.send(handle, message.dump(), websocketpp::frame::opcode::text);
  };
  server.set_message_handler([&](websocketpp::connection_hdl handle,
                                  Server::message_ptr message) {
    const auto data = ParseMessage(message->get_payload());
    const auto id = data.value("id", std::string{});
    const auto method = data.value("method", std::string{});
    if (method == "session.hello") {
      send(handle, Response(id, {{"session_id", "test-session"}}));
    } else if (method == "room.join") {
      send(handle, MakeRequest("early-offer", "sfu.offer", subscription));
      server.set_timer(250, [&, handle, id](const websocketpp::lib::error_code& ec) {
        if (ec) return;
        join_sent = true;
        send(handle, Response(id, {{"media_mode", "sfu"}}));
      });
    } else if (id == "early-offer" || id == "repeat-offer") {
      if (finished) return;
      const bool ok = join_sent && data.value("ok", false) &&
          data.value("data", Json::object()).value("sdp", std::string{})
              .find("m=video") != std::string::npos;
      if (ok && id == "early-offer") {
        send(handle, MakeRequest("repeat-offer", "sfu.offer", subscription));
      } else {
        finished = true;
        result.set_value(ok);
      }
    }
  });

  server.start_accept();
  std::thread listener([&] { server.run(); });
  rtcwin_options options{};
  options.struct_size = sizeof(options);
  options.abi_version = RTCWIN_ABI_VERSION;
  struct TrackEvents {
    std::atomic<int> screens{0};
    std::atomic<int> cameras{0};
    std::atomic<int> unlabelled{0};
  } tracks;
  options.userdata = &tracks;
  options.on_event = [](void* userdata, const char* method, const char* body) {
    if (std::string_view(method) != "media.track") return;
    const auto track = ParseMessage(body).value("data", Json::object());
    if (track.value("pc", "") != "sfu-recv" ||
        track.value("kind", "") != "video") return;
    auto& events = *static_cast<TrackEvents*>(userdata);
    const auto uid = track.value("uid", std::string{});
    const auto source = track.value("source", std::string{});
    if (uid == "A" && source == "screen") {
      ++events.screens;
    } else if (uid == "B" && source == "camera") {
      ++events.cameras;
    } else {
      ++events.unlabelled;
    }
  };
  std::unique_ptr<rtcwin_handle, decltype(&rtcwin_destroy)> client(
      rtcwin_create(&options), rtcwin_destroy);
  const std::string join = Json{
      {"url", "ws://127.0.0.1:" + std::to_string(port)},
      {"user_id", "C"}, {"room_id", "switch-test"}, {"token", "local-test"},
      {"audio_backend", "fake"}}.dump();
  const bool ok = client &&
      rtcwin_submit(client.get(), "session.connectAndJoin", join.c_str(), 5000) &&
      completed.wait_for(6s) == std::future_status::ready && completed.get();
  client.reset();
  server.stop();
  listener.join();
  const bool labelled = tracks.screens == 2 && tracks.cameras == 2 &&
                        tracks.unlabelled == 0;
  if (!ok) std::cerr << "Expected video answers for early and repeated SFU offers\n";
  if (!labelled) std::cerr << "SFU tracks must retain their consumer identity\n";
  return ok && labelled ? 0 : 1;
}
