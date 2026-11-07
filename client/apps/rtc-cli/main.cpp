#include "rtc_client/domain/session.h"
#include "rtc_client/domain/notifications.h"
#include "rtc_client/logging/logger.h"
#include "rtc_client/signaling/rpc.h"
#include "rtc_client/signaling/transport.h"
#include "rtc_client/media/worker.h"
#include <asio.hpp>
#include <csignal>
#include <cerrno>
#include <iostream>
#include <poll.h>
#include <unistd.h>
using namespace rtc_client;
using Json = protocol::Json;
using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;

namespace {
struct Options {
  TransportOptions transport;
  std::string room, display{"RK signaling probe"};
  std::string log_file{"/tmp/rtc-rk-client/signaling-" + std::to_string(getpid()) + ".log"};
  unsigned duration{3};
  bool signaling_only{false}, token_stdin{false};
  bool audio{false}, unmute{false};
  media::AudioOptions audio_options;
};
bool room_id(const std::string& room) {
  if (room.empty() || room.size() > 64) return false;
  for (unsigned char c : room)
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') ||
          c == '_' || c == '-' || c == '.')) return false;
  return true;
}
void usage() {
  std::cout << "rtc-cli --signaling-only --token-stdin --url wss://host/talk/v1/ws\n"
               "        [--room ID] [--display NAME] [--duration 1..60] [--ca-file FILE]\n"
               "        [--log-file FILE] [--development-allow-ws]\n"
               "Or use --audio instead of --signaling-only in a WebRTC build:\n"
               "        --audio-backend alsa|pulse --recording-device ID --playout-device ID\n"
               "        [--unmute] (default: muted; explicit server authorization required)\n"
#ifdef RTC_ENABLE_TEST_AUDIO
               "        [--development-fake-audio] (synthetic 440Hz; no real devices)\n"
#endif
               "Video receive is not implemented in this milestone. No video is sent.\n"
               "Token must be piped on stdin, never supplied as an argument.\n";
}
Options parse(int argc, char** argv) {
  Options out;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--signaling-only") { out.signaling_only = true; continue; }
    if (arg == "--audio") { out.audio = true; continue; }
    if (arg == "--unmute") { out.unmute = true; continue; }
#ifdef RTC_ENABLE_TEST_AUDIO
    if (arg == "--development-fake-audio") { out.audio_options.backend = "fake"; continue; }
#endif
    if (arg == "--token-stdin") { out.token_stdin = true; continue; }
    if (arg == "--development-allow-ws") { out.transport.development_allow_ws = true; continue; }
    if (i + 1 >= argc) throw std::runtime_error("missing option value");
    const std::string value = argv[++i];
    if (arg == "--url") out.transport.url = value;
    else if (arg == "--room") out.room = value;
    else if (arg == "--display") out.display = value;
    else if (arg == "--ca-file") out.transport.ca_file = value;
    else if (arg == "--log-file") out.log_file = value;
    else if (arg == "--audio-backend") {
      if(value!="alsa" && value!="pulse") throw std::runtime_error("invalid audio backend");
      out.audio_options.backend = value;
    }
    else if (arg == "--recording-device") out.audio_options.recording_device = value;
    else if (arg == "--playout-device") out.audio_options.playout_device = value;
    else if (arg == "--duration") {
      std::size_t consumed{};
      const auto seconds = std::stoul(value,&consumed);
      if (consumed != value.size() || seconds < 1 || seconds > 60) throw std::runtime_error("invalid duration");
      out.duration = static_cast<unsigned>(seconds);
    } else throw std::runtime_error("unknown option");
  }
  if (out.signaling_only == out.audio || !out.token_stdin || !WsTransport::valid_options(out.transport) ||
      (!out.room.empty() && !room_id(out.room)) || out.display.size() > 64)
    throw std::runtime_error("invalid options");
  if(out.unmute && !out.audio)throw std::runtime_error("unmute requires audio");
  if(out.audio && out.room.empty())throw std::runtime_error("audio requires room");
#ifndef RTC_WITH_WEBRTC
  if(out.audio)throw std::runtime_error("WebRTC backend not built");
#endif
  return out;
}
std::string read_token() {
  if (isatty(STDIN_FILENO)) throw std::runtime_error("token must be piped on stdin");
  std::string token;
  const auto deadline = Clock::now() + 8s;
  char c;
  for (;;) {
    const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now());
    if (remaining.count() <= 0) throw std::runtime_error("token input timeout");
    pollfd input{STDIN_FILENO,POLLIN,0};
    const auto ready = poll(&input,1,static_cast<int>(remaining.count()));
    if (ready < 0 && errno == EINTR) continue;
    if (ready <= 0) throw std::runtime_error("token input unavailable");
    const auto bytes = read(STDIN_FILENO,&c,1);
    if (bytes == 0) break;
    if (bytes < 0 && errno == EINTR) continue;
    if (bytes < 0) throw std::runtime_error("token input failure");
    if (c == '\n') break;
    if (token.size() >= 8192 || static_cast<unsigned char>(c) < 0x21 || static_cast<unsigned char>(c) > 0x7e)
      throw std::runtime_error("invalid token input");
    token += c;
  }
  if (token.empty()) throw std::runtime_error("empty token input");
  return token;
}

// CLI composition root. Both signaling and media use the same authenticated
// control plane; blocking SDK operations NEVER execute on the Asio executor.
class Probe final {
 public:
  explicit Probe(Options options)
      : options_(std::move(options)), logger_(options_.log_file), transport_(io_),
        rpc_([this](const std::string& text) { return transport_.send(text); }),
        tick_(io_), heartbeat_(io_), stay_(io_), overall_(io_), stats_(io_), signals_(io_,SIGINT,SIGTERM) {
#ifdef RTC_WITH_WEBRTC
    if(options_.audio)audio_=std::make_unique<media::AudioWorker>(io_.get_executor(),gate_,
      [this](std::string name,Json data) { if(!ending_)event(name.c_str(),std::move(data)); },
      media::make_webrtc_audio);
#endif
    rpc_.set_handlers([this](const ServerRequest& request) {
      if(audio_ && request.method=="sfu.offer" &&
          (lifecycle_.state()==SessionState::joining || lifecycle_.state()==SessionState::joined)) {
        try {
          const auto sdp=request.data.at("sdp").get<std::string>();
          const auto count=request.data.value("consumers",Json::array()).size();
          if(!audio_->submit([sdp](media::Audio& m){return m.answer(sdp);},
              [this,request,count](media::Result result) {
                if(ending_ || lifecycle_.state()==SessionState::leaving)return;
                if(!rpc_.respond(request,{result.ok(),result.code,result.data},Clock::now())){fail(504);return;}
                event("media.subscribed",{{"ok",result.ok()},{"code",result.code},{"consumer_count",count}});
                if(!result.ok())fail(result.code);
              }))rpc_.respond(request,{false,429,Json::object()},Clock::now());
        } catch(...) {rpc_.respond(request,{false,400,Json::object()},Clock::now());}
        return;
      }
      const auto code = request.method == "sfu.offer" ? 488 : 501;
      rpc_.respond(request,{false,code,Json::object()},Clock::now());
      event("media.unavailable",{{"code",code},{"reason","signaling_only"}});
      media_requested_ = true;
    },[this](const protocol::Message& message) {
      if(message.method=="media.forceMute" || message.method=="floor.revoked" ||
          message.method=="room.policyChanged") {
        gate_->close();
        if(audio_ && !audio_->submit([](media::Audio& m){m.mute(true);return Json::object();},
            [this](media::Result result){if(!ending_ && !result.ok())fail(result.code);}))fail(429);
        event("audio.muted",{{"muted",true}});
      }
      if (message.method == "session.kicked") fail(403);
      else if (message.method == "room.closed") {
        const auto reason = message.data.find("reason");
        if (reason == message.data.end() || !reason->is_string()) { fail(400); return; }
        if (!expected_room_closed(lifecycle_.state(),reason->get<std::string>())) {
          event("room.closed");
          fail(409);
        }
      }
    });
  }
  int run(std::string token) {
    lifecycle_.apply(SessionEvent::connect);
    const auto connecting = lifecycle_.stamp();
    signals_.async_wait([this](const asio::error_code& ec,int) { if (!ec) { exit_code_ = 130; shutdown(); } });
    overall_.expires_after(std::chrono::seconds(options_.duration + 45));
    overall_.async_wait([this](const asio::error_code& ec) {
      if (!ec) { gate_->close(); if(audio_)audio_->stop(); exit_code_ = 2; event("error",{{"code",504}}); io_.stop(); }
    });
    tick();
    event("session.connecting");
    const bool accepted = transport_.connect(options_.transport,std::move(token),{
      [this,connecting] {
        if (!lifecycle_.complete(connecting,SessionEvent::transport_open)) return;
        event("session.authenticating");
        call("session.hello",{{"client",{{"platform","linux"},{"version","0.1.0"},{"sdk",audio_?"webrtc-m146":"signaling-only"}}},
          {"caps",{{"audio",{{"codecs",audio_?Json::array({"opus"}):Json::array()},
                            {"aec",audio_ && options_.audio_options.backend!="fake"?"webrtc":"none"}}},
          {"video",{{"codecs",Json::array()},{"max_recv",0}}},{"p2p",false},{"p2p_video",false},{"sfu_sdp","unified-plan"}}}},
          [this](const Json& data) { ready(data); });
      },
      [this](std::string text) { rpc_.receive(protocol::decode(text),Clock::now()); },
      [this](TransportClose close) {
        gate_->close();if(audio_)audio_->stop();
        if (!ending_) { exit_code_ = 2; event("error",{{"code",480},{"http_status",close.http_status}}); }
        rpc_.close();
        event("session.closed",{{"close_code",close.close_code}});
        io_.stop();
      }
    });
    if (!accepted) { ending_ = true; exit_code_ = 2; io_.stop(); event("error",{{"code",400}}); }
    io_.run();
    return exit_code_ ? exit_code_ : media_requested_ ? 3 : 0;
  }
 private:
  void event(const char* name, Json data = Json::object()) {
    data["signaling_only"] = options_.signaling_only;
    std::cout << Json({{"schema",1},{"sequence",++event_sequence_},
      {"generation",lifecycle_.stamp().generation},{"name",name},{"data",std::move(data)}}).dump() << std::endl;
    logging::emit(logging::Level::info,name,{{"generation",lifecycle_.stamp().generation}});
  }
  void call(const std::string& method, const Json& data, std::function<void(const Json&)> success) {
    const auto stamp = lifecycle_.stamp();
    auto id = rpc_.begin(method,data,[this,stamp,success=std::move(success)](RpcResult result) {
      if (ending_ || !lifecycle_.accepts(stamp)) return;
      if (!result.ok) { fail(result.code); return; }
      try { success(result.data); } catch (...) { fail(400); }
    },Clock::now(),method.rfind("sfu.",0)==0 ? 15s : 10s);
    if (id.empty()) fail(429);
  }
  void ready(const Json& data) {
    if (!data.at("session_id").is_string() || !data.at("uid").is_string()) { fail(400); return; }
    const auto seconds = data.value("heartbeat_interval",15);
    if (seconds < 1 || seconds > 30) { fail(400); return; }
    heartbeat_seconds_ = seconds;
    lifecycle_.apply(SessionEvent::hello_new);
    event("session.ready");
    heartbeat();
    if(audio_) {
      if(!audio_->initialize(options_.audio_options,data.value("ice_servers",Json::array()),
          [this](media::Result result) {
            if(ending_)return;
            if(!result.ok()){fail(result.code);return;}
            event("audio.ready",{{"backend",options_.audio_options.backend},{"muted",true}});
            join();
          }))fail(429);
    } else join();
  }
  void join() {
    if (options_.room.empty()) stay();
    else {
      lifecycle_.apply(SessionEvent::join);
      call("room.join",{{"room_id",options_.room},{"display",options_.display},
        {"media",{{"audio",{{"send",bool(audio_)},{"muted",true}}},{"video",{{"send",false}}}}},
        {"policy",{{"media_mode","sfu"},{"talk_mode","free"}}}},
        [this](const Json& result) {
          if (result.at("room").at("room_id") != options_.room || result.value("media_mode","") != "sfu") { fail(488); return; }
          lifecycle_.apply(SessionEvent::join_ok);
          event("room.joined",{{"peer_count",result.value("peers",Json::array()).size()}});
          if(audio_)publish();else stay();
        });
    }
  }
  void publish() {
    if(!audio_->submit([](media::Audio& m){return m.offer();},[this](media::Result offer) {
      if(ending_)return;
      if(!offer.ok()){fail(offer.code);return;}
      call("sfu.publish",offer.data,[this](const Json& answer) {
        const auto sdp=answer.at("sdp").get<std::string>();
        if(!audio_->submit([sdp](media::Audio& m){m.accept(sdp);return Json::object();},
            [this](media::Result applied) {
              if(ending_)return;
              if(!applied.ok()){fail(applied.code);return;}
              event("audio.published",{{"muted",true}});
              if(options_.unmute)unmute();else {collect_stats();stay();}
            }))fail(429);
      });
    }))fail(429);
  }
  void unmute() {
    const auto ticket=gate_->begin_open_request();
    call("media.setMute",{{"source","mic"},{"muted",false}},[this,ticket](const Json&) {
      if(!audio_->submit([](media::Audio& m){m.mute(false);return Json::object();},
          [this,ticket](media::Result result) {
            if(ending_)return;
            if(!result.ok()){fail(result.code);return;}
            const bool opened=gate_->open_if_current(ticket);
            event("audio.muted",{{"muted",!opened}});
            collect_stats();stay();
          }))fail(429);
    });
  }
  void collect_stats() {
    stats_.expires_after(2s);
    stats_.async_wait([this](const asio::error_code& ec) {
      if(ec || ending_ || lifecycle_.state()!=SessionState::joined)return;
      if(!audio_->submit([](media::Audio& m){return m.stats();},[this](media::Result result) {
        if(ending_ || lifecycle_.state()!=SessionState::joined)return;
        if(!result.ok()){fail(result.code);return;}
        event("audio.stats",std::move(result.data));collect_stats();
      }))fail(429);
    });
  }
  void stay() {
    stay_.expires_after(std::chrono::seconds(options_.duration));
    stay_.async_wait([this](const asio::error_code& ec) {
      if (ec || ending_) return;
      heartbeat_.cancel();
      gate_->close();stats_.cancel();if(audio_)audio_->stop();
      if (lifecycle_.state() == SessionState::joined) {
        lifecycle_.apply(SessionEvent::leave);
        call("room.leave",{{"room_id",options_.room}},[this](const Json&) {
          lifecycle_.apply(SessionEvent::leave_ok);
          event("room.left");
          bye();
        });
      } else bye();
    });
  }
  void bye() { call("session.bye",Json::object(),[this](const Json&) { event("session.bye"); shutdown(); }); }
  void heartbeat() {
    heartbeat_.expires_after(std::chrono::seconds(heartbeat_seconds_));
    heartbeat_.async_wait([this](const asio::error_code& ec) {
      if (!ec && !ending_) call("session.ping",Json::object(),[this](const Json&) { heartbeat(); });
    });
  }
  void tick() {
    tick_.expires_after(25ms);
    tick_.async_wait([this](const asio::error_code& ec) {
      if (!ec && !ending_) { rpc_.tick(Clock::now()); tick(); }
    });
  }
  void fail(int code) { if (!ending_) { exit_code_ = 2; event("error",{{"code",code}}); shutdown(); } }
  void shutdown() {
    if (ending_) return;
    ending_ = true;
    gate_->close();if(audio_)audio_->stop();
    lifecycle_.apply(SessionEvent::stop);
    tick_.cancel(); heartbeat_.cancel(); stay_.cancel(); stats_.cancel(); signals_.cancel();
    rpc_.close();
    transport_.close();
    // The bounded close timer or the overall guard remains active until the
    // closed callback stops io. No infinite shutdown wait or forced thread kill.
  }
  Options options_;
  logging::Runtime logger_;
  asio::io_context io_;
  WsTransport transport_;
  RpcPeer rpc_;
  SessionLifecycle lifecycle_;
  asio::steady_timer tick_, heartbeat_, stay_, overall_, stats_;
  asio::signal_set signals_;
  std::shared_ptr<MicrophoneGate> gate_=std::make_shared<MicrophoneGate>();
  std::unique_ptr<media::AudioWorker> audio_;
  unsigned heartbeat_seconds_{15};
  std::uint64_t event_sequence_{};
  bool ending_{false}, media_requested_{false};
  int exit_code_{};
};
}  // namespace

int main(int argc,char** argv) {
  if (argc == 2 && std::string(argv[1]) == "--help") { usage(); return 0; }
  try {
    auto options = parse(argc,argv);
    auto token = read_token();
    Probe probe(std::move(options));
    return probe.run(std::move(token));
  } catch (...) { std::cerr << "rtc-cli: invalid options/input or initialization failure; use --help\n"; return 2; }
}
