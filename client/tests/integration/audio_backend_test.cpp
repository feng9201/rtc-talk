#include "rtc_client/media/audio.h"
#include "test.h"
#ifdef RTC_TEST_WITH_TLS
#include <asio/ssl.hpp>
#endif
using namespace rtc_client;
using namespace rtc_client::media;
int main() { return test_main([] {
#ifdef RTC_TEST_WITH_TLS
  asio::ssl::context context(asio::ssl::context::tls_client);
  context.set_verify_mode(asio::ssl::verify_peer);
  CHECK(context.native_handle()!=nullptr);
#endif
  auto gate=std::make_shared<MicrophoneGate>();
  AudioOptions options;options.backend="fake";
  auto audio=make_webrtc_audio(options,Json::array(),gate,[](std::string,Json){});
  CHECK(!gate->is_open());
  const auto offer=audio->offer();
  CHECK(offer.at("tracks").size()==1);
  CHECK(offer.at("tracks")[0].at("kind")=="audio");
  const auto sdp=offer.at("sdp").get<std::string>();
  CHECK(sdp.find("opus/48000")!=std::string::npos);
  CHECK(sdp.find("m=video ")==std::string::npos);
  CHECK(!gate->is_open());
  const auto ticket=gate->begin_open_request();CHECK(gate->open_if_current(ticket));
  audio->mute(true);CHECK(!gate->is_open());CHECK(!gate->open_if_current(ticket));
  bool rejected=false;
  try {audio->answer("v=0\r\nm=video 9 UDP/TLS/RTP/SAVPF 102\r\n");}
  catch(const Error& e){rejected=e.code==488;}
  CHECK(rejected);CHECK(!gate->is_open());
  CHECK(audio->stats().contains("send"));
  audio.reset();CHECK(!gate->is_open());
}); }
