// File: core/media/media_controller.cpp
// Brief: 实现 SFU/P2P 编排、静音与模式切换。
#include "media/media_controller.h"

#include <chrono>
#include <cmath>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

#include "base/log.h"

namespace talk {
namespace {

using namespace std::chrono_literals;

// 服务端要求切回 SFU 后，最多等这么久的新 SDP。
constexpr auto kSfuSwitchTimeout = 2s;

// 音频接收诊断日志的采样周期。
constexpr auto kStatsLogInterval = 5s;

// 视频 m-line 摘要的最大长度，避免日志被 SDP 撑爆。
constexpr size_t kSdpSummaryLimit = 900;

// 挑出排查视频上行必需的行：编码、fmtp 参数、方向、mid 与 ssrc 数量。
// 刻意不输出 a=fingerprint / a=candidate，避免泄露与噪声。
std::string SdpVideoSummary(const std::string& sdp) {
  std::istringstream in(sdp);
  std::string line;
  std::string out;
  bool video = false;
  bool appended_m = false;
  size_t ssrc = 0;
  const auto append = [&out](const std::string& piece) {
    if (out.size() >= kSdpSummaryLimit) {
      return;
    }
    if (!out.empty()) {
      out += " | ";
    }
    out += piece;
  };
  while (std::getline(in, line)) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    if (line.rfind("m=", 0) == 0) {
      if (video) {
        append("ssrc=" + std::to_string(ssrc));
      }
      video = line.rfind("m=video", 0) == 0;
      ssrc = 0;
      appended_m = false;
      continue;
    }
    if (!video) {
      continue;
    }
    if (!appended_m) {
      // m= 行本身只保留类型与端口，PT 列表在 rtpmap 里已给出。
      append("m=video");
      appended_m = true;
    }
    if (line.rfind("a=mid:", 0) == 0 ||
        line.rfind("a=rtpmap:", 0) == 0 || line.rfind("a=fmtp:", 0) == 0 ||
        line.rfind("a=sendonly", 0) == 0 || line.rfind("a=inactive", 0) == 0 ||
        line.rfind("a=recvonly", 0) == 0) {
      append(line);
    } else if (line.rfind("a=ssrc", 0) == 0) {
      ++ssrc;
    }
  }
  if (video) {
    append("ssrc=" + std::to_string(ssrc));
  }
  return out;
}

}  // namespace

MediaController::MediaController(asio::io_context& io, const Json& config,
                                 EventSink& events, VideoFrameSink video_sink,
                                 VideoFrameSink local_video_sink)
    : io_(io),
      events_(events),
      queue_(io, config, std::make_shared<std::atomic<bool>>(true),
             [this](std::string name, Json data) {
               HandleMediaEvent(std::move(name), std::move(data));
             },
             std::move(video_sink), std::move(local_video_sink)),
      mode_timer_(io),
      stats_timer_(io) {}

MediaController::~MediaController() { StopStatsLogging(); }

void MediaController::SetSignaling(MediaSignaling signaling) {
  signaling_ = std::move(signaling);
}

void MediaController::SetIceServers(const Json& ice_servers) {
  queue_.SetIceServers(ice_servers);
  TALK_LOG_DEBUG("media", "应用 ICE 配置 count={}", ice_servers.size());
}

void MediaController::Reset() {
  TALK_LOG_INFO("media", "重置媒体控制器");
  ++epoch_;
  ++p2p_epoch_;
  mode_ = Mode::kSfu;
  p2p_session_.clear();
  p2p_peer_.clear();
  p2p_offerer_ = false;
  p2p_started_ = false;
  p2p_connected_ = false;
  consumers_ = Json::object();
  on_sfu_ready_ = nullptr;
  mode_timer_.cancel();
  StopStatsLogging();
  stats_baselines_.clear();
  stats_no_recv_warned_.clear();
  video_capture_baseline_ = 0;
  video_capture_warned_ = false;
  muted_ = true;
  audio_capture_baseline_ = 0;
  audio_idle_cycles_ = 0;
  audio_idle_warned_ = false;
  queue_.Reset();
}

void MediaController::Invalidate() {
  TALK_LOG_DEBUG("media", "作废在途媒体结果 epoch={}", epoch_);
  ++epoch_;
}

void MediaController::Shutdown() {
  TALK_LOG_INFO("media", "关闭媒体控制器");
  mode_timer_.cancel();
  StopStatsLogging();
  queue_.Shutdown();
}

void MediaController::ApplyModeView(const Json& view) {
  if (!view.is_object()) {
    return;
  }
  mode_ = view.value("media_mode", std::string("sfu")) == "p2p"
              ? Mode::kP2p
              : Mode::kSfu;
  if (mode_ == Mode::kP2p && view.contains("p2p") &&
      view["p2p"].is_object()) {
    const Json& p2p = view["p2p"];
    p2p_session_ = p2p.value("session", std::string());
    p2p_peer_ = p2p.value("peer", std::string());
    p2p_offerer_ = p2p.value("role", std::string()) == "offerer";
  }
  TALK_LOG_INFO("media", "应用模式 mode={} session={} peer={} role={}",
                mode_ == Mode::kP2p ? "p2p" : "sfu", p2p_session_,
                p2p_peer_, p2p_offerer_ ? "offerer" : "answerer");
}

void MediaController::Publish() {
  if (!queue_.enabled()) {
    return;
  }
  const uint64_t epoch = epoch_;
  Post(
      [](Media& media) { return media.Offer(); },
      [this, epoch](MediaResult offer) {
        if (epoch != epoch_) {
          TALK_LOG_WARN("media", "offer 结果已作废 epoch={}", epoch);
          return;
        }
        if (!offer.ok) {
          EmitMediaError("生成 offer 失败", offer.error);
          return;
        }
        const std::string sdp =
            offer.data.is_object()
                ? offer.data.value("sdp", std::string())
                : std::string();
        if (sdp.empty()) {
          EmitMediaError("生成 offer 失败", "SDP 为空");
          return;
        }
        TALK_LOG_INFO("media", "SFU offer 已生成 sdp={}", sdp.size());
        TALK_LOG_INFO("media", "SFU offer 视频行 {}", SdpVideoSummary(sdp));
        TALK_LOG_INFO("media", "SFU publish tracks={}",
                      offer.data.contains("tracks")
                          ? offer.data["tracks"].dump()
                          : std::string("<missing>"));
        if (!signaling_.request) {
          return;
        }
        signaling_.request("sfu.publish", offer.data, [this, epoch](Json a) {
          if (epoch != epoch_) {
            TALK_LOG_WARN("media", "publish 应答已作废 epoch={}", epoch);
            return;
          }
          if (!a.value("ok", false)) {
            const Json error = a.value("error", Json::object());
            TALK_LOG_ERROR("media", "SFU 发布失败 code={} msg={}",
                           error.value("code", 500),
                           error.value("msg", std::string()));
            events_.Emit("media.error",
                         {{"msg", "发布失败"},
                          {"code", error.value("code", 500)},
                          {"detail", error.value("msg", std::string())}});
            return;
          }
          const Json data = a.value("data", Json::object());
          const std::string answer = data.value("sdp", std::string());
          // 只打 producer 摘要：mid/paused 能直接看出服务端是否把两路视频分开。
          TALK_LOG_INFO("media", "SFU 发布 producer 数={} 摘要={}",
                        data.value("producers", Json::array()).size(),
                        data.value("producers", Json::array()).dump());
          TALK_LOG_INFO("media", "SFU 发布成功 answer={}", answer.size());
          TALK_LOG_INFO("media", "SFU answer 视频行 {}",
                        SdpVideoSummary(answer));
          Post([answer](Media& media) { return media.Accept(answer); },
               [this, epoch](MediaResult applied) {
                 if (epoch != epoch_) {
                   TALK_LOG_WARN("media", "apply answer 已作废 epoch={}",
                                 epoch);
                   return;
                 }
                 if (!applied.ok) {
                   EmitMediaError("应用应答失败", applied.error);
                   return;
                 }
                 TALK_LOG_INFO("media", "SFU 发布完成");
                 // 发布前收到的 setMute 可能因媒体实例尚未创建被丢弃，
                 // 这里按控制器的静音目标再补一次。
                 if (!muted_.load()) {
                   SetMuted(false);
                 }
                 events_.Emit("published", {{"ok", true}});
               });
        });
      });
}

void MediaController::ApplySfuOffer(const std::string& sdp,
                                    const Json& consumers,
                                    std::function<void(MediaResult)> done) {
  const uint64_t epoch = epoch_;
  TALK_LOG_INFO("media", "应用 SFU offer sdp={} consumers={}", sdp.size(),
                consumers.size());
  Post([sdp](Media& media) { return media.Answer(sdp); },
       [this, epoch, consumers, done = std::move(done)](
           MediaResult result) mutable {
         if (epoch != epoch_) {
           if (done) {
             done(MediaResult{false, Json::object(), 487, "媒体操作已取消"});
           }
           return;
         }
         if (!result.ok) {
           EmitMediaError("接收 SDP 失败", result.error);
           if (done) {
             done(std::move(result));
           }
           return;
         }
         TALK_LOG_INFO("media", "SFU offer 已应答 consumers={}",
                       consumers.size());
         consumers_ = Json::object();
         if (consumers.is_array()) {
           for (const auto& item : consumers) {
             if (!item.is_object() || !item.contains("mid") ||
                 !item["mid"].is_string()) {
               continue;
             }
             const std::string mid = item["mid"].get<std::string>();
             consumers_[mid] = item;
             // 界面按 mid 建画面，需要 uid/source 才能标出对端与采集源。
             if (item.value("kind", std::string()) == "video") {
               events_.Emit("video.track",
                            {{"mid", mid},
                             {"uid", item.value("uid", std::string())},
                             {"source", item.value("source", std::string())}});
             }
           }
         }
         // 订阅已建立：开始周期统计，确认远端 RTP 是否到达。
         StartStatsLogging();
         if (done) {
           done(std::move(result));
         }
       });
}

void MediaController::StartP2p() {
  if (!queue_.enabled()) {
    return;
  }
  if (p2p_session_.empty() || p2p_peer_.empty()) {
    events_.Emit("media.error", {{"msg", "P2P 会话参数缺失"}});
    return;
  }
  const uint64_t epoch = epoch_;
  const uint64_t seq = ++p2p_epoch_;
  const std::string session = p2p_session_;
  const std::string peer = p2p_peer_;
  const bool offerer = p2p_offerer_;
  TALK_LOG_INFO("media", "启动 P2P session={} peer={} role={}", session,
                peer, offerer ? "offerer" : "answerer");
  Post([offerer](Media& media) { return media.StartP2p(offerer); },
       [this, epoch, seq, session, peer, offerer](MediaResult result) {
         if (epoch != epoch_ || seq != p2p_epoch_ || mode_ != Mode::kP2p) {
           return;
         }
         std::string why = result.ok ? std::string() : result.error;
         const std::string sdp =
             result.data.is_object()
                 ? result.data.value("sdp", std::string())
                 : std::string();
         if (why.empty() && offerer && sdp.empty()) {
           why = "SDP 为空";
         }
         if (!why.empty()) {
           EmitMediaError("P2P 初始化失败", why);
           if (signaling_.notify) {
             signaling_.notify("p2p.state",
                               {{"session", session}, {"state", "failed"}});
           }
           return;
         }
         p2p_started_ = true;
         TALK_LOG_INFO("media", "P2P 已启动 session={} role={}", session,
                       offerer ? "offerer" : "answerer");
         events_.Emit("p2p.started",
                      {{"session", session},
                       {"peer", peer},
                       {"role", offerer ? "offerer" : "answerer"}});
         if (offerer && !sdp.empty() && signaling_.notify) {
           signaling_.notify("p2p.offer", {{"session", session},
                                           {"to", peer},
                                           {"sdp", sdp}});
         }
       });
}

void MediaController::SwitchToP2p(const Json& allocation) {
  TALK_LOG_INFO("media", "切换到 P2P");
  ++p2p_epoch_;
  ApplyModeView({{"media_mode", "p2p"}, {"p2p", allocation}});
  StartP2p();
}

void MediaController::AdoptP2pSession(const std::string& session,
                                      const std::string& peer) {
  p2p_session_ = session;
  p2p_peer_ = peer;
  p2p_offerer_ = false;
  mode_ = Mode::kP2p;
  TALK_LOG_INFO("media", "接管 P2P 会话 session={} peer={}", session,
                peer);
}

void MediaController::AnswerP2p(const std::string& session,
                                const std::string& from,
                                const std::string& sdp) {
  const uint64_t epoch = epoch_;
  const uint64_t seq = p2p_epoch_;
  TALK_LOG_INFO("media", "应答 P2P offer session={} from={} sdp={}",
                session, from, sdp.size());
  Post(
      [sdp](Media& media) {
        // 重协商的 offer 需要全新 PC，旧 PC 的 DTLS 指纹对不上。
        media.CloseP2p();
        return media.AnswerP2p(sdp);
      },
      [this, epoch, seq, session, from](MediaResult result) {
        if (epoch != epoch_ || seq != p2p_epoch_) {
          return;
        }
        std::string why = result.ok ? std::string() : result.error;
        if (why.empty() &&
            (!result.data.is_object() || !result.data.contains("sdp"))) {
          why = "SDP 为空";
        }
        if (!why.empty()) {
          EmitMediaError("P2P answer 失败", why);
          if (signaling_.notify) {
            signaling_.notify("p2p.state",
                              {{"session", session}, {"state", "failed"}});
          }
          return;
        }
        const std::string answer = result.data.value("sdp", std::string());
        if (!answer.empty() && signaling_.notify) {
          TALK_LOG_INFO("media", "P2P answer 已发送 session={} sdp={}",
                        session, answer.size());
          signaling_.notify("p2p.answer", {{"session", session},
                                           {"to", from},
                                           {"sdp", answer}});
        }
      });
}

void MediaController::AcceptP2pAnswer(const std::string& session,
                                      const std::string& sdp) {
  if (session != p2p_session_ || !p2p_offerer_ || sdp.empty()) {
    return;
  }
  const uint64_t epoch = epoch_;
  const uint64_t seq = p2p_epoch_;
  TALK_LOG_INFO("media", "应用 P2P answer sdp={}", sdp.size());
  Post([sdp](Media& media) { return media.AcceptP2p(sdp); },
       [this, epoch, seq](MediaResult result) {
         if (epoch != epoch_ || seq != p2p_epoch_) {
           return;
         }
         if (!result.ok) {
           EmitMediaError("应用 P2P answer 失败", result.error);
         }
       });
}

void MediaController::AddP2pCandidate(const std::string& session,
                                      const std::string& candidate,
                                      const std::string& mid, int index) {
  if (session != p2p_session_) {
    return;
  }
  Post(
      [candidate, mid, index](Media& media) {
        media.AddP2pCandidate(candidate, mid, index);
        return Json::object();
      },
      nullptr, false);
}

void MediaController::HandleP2pClose(const Json& data) {
  if (!p2p_session_.empty() &&
      data.value("session", std::string()) != p2p_session_) {
    return;
  }
  TALK_LOG_INFO("media", "关闭 P2P session={}", p2p_session_);
  CloseP2p();
  events_.Emit("p2p.close", data);
}

void MediaController::SwitchToSfu(std::function<void()> ready) {
  TALK_LOG_INFO("media", "切换到 SFU");
  ++p2p_epoch_;
  mode_ = Mode::kSfu;
  p2p_connected_ = false;
  const uint64_t seq = ++switch_seq_;
  on_sfu_ready_ = std::move(ready);
  // 复用原有 send/recv PC：mediasoup transport 不接受第二次 DTLS connect。
  SetSfuPaused(false, [this, seq] {
    if (seq != switch_seq_ || !on_sfu_ready_) {
      return;
    }
    Publish();
  });
  mode_timer_.expires_after(kSfuSwitchTimeout);
  mode_timer_.async_wait([this, seq](const asio::error_code& error) {
    if (error || seq != switch_seq_) {
      return;
    }
    TALK_LOG_WARN("media", "等待 SFU 订阅超时，按已就绪处理");
    CompleteSfuSwitch();
  });
}

void MediaController::FinishModeSwitch() {
  // 只处理 room.mediaMode=sfu 引发的订阅；普通 sfu.offer 不改链路。
  if (!on_sfu_ready_) {
    return;
  }
  TALK_LOG_INFO("media", "SFU 模式切换完成");
  CompleteSfuSwitch();
  if (mode_ == Mode::kSfu && !p2p_session_.empty()) {
    CloseP2p();
  }
}

void MediaController::SetSfuPaused(bool paused, std::function<void()> done) {
  TALK_LOG_DEBUG("media", "SFU 暂停 paused={}", paused);
  Post([paused](Media& media) {
         media.SfuPause(paused);
         return Json::object();
       },
       [done = std::move(done)](MediaResult) {
         if (done) {
           done();
         }
       },
       false);
}

void MediaController::SetMuted(bool muted,
                               std::function<void(MediaResult)> done) {
  TALK_LOG_INFO("media", "设置静音 muted={}", muted);
  muted_ = muted;
  // 每次开麦重新给一轮判定机会，避免上一段静音期的零采集把告警吃掉。
  audio_idle_cycles_ = 0;
  audio_idle_warned_ = false;
  Post([muted](Media& media) {
         media.SetMuted(muted);
         return Json::object();
       },
       std::move(done), false);
}

void MediaController::StartVideo(const std::string& source,
                                 std::function<void(MediaResult)> done) {
  TALK_LOG_INFO("media", "启动视频 source={}", source);
  Post([source](Media& media) { return media.StartVideo(source); },
       [done = std::move(done), source](MediaResult result) mutable {
         if (result.ok && result.data.is_object()) {
           TALK_LOG_INFO("media", "视频已启动 source={} sharing={} send_state={}",
                         source, result.data.value("sharing", false),
                         result.data.value("send_state", std::string()));
         } else {
           TALK_LOG_ERROR("media", "视频启动失败 source={} ok={} error={}",
                          source, result.ok, result.error);
         }
         if (done) {
           done(std::move(result));
         }
       });
}

void MediaController::StopVideo(const std::string& source,
                                std::function<void(MediaResult)> done) {
  TALK_LOG_INFO("media", "停止视频 source={}", source);
  // 不建新实例：媒体未创建时无需停采，直接回报成功。
  Post([source](Media& media) { return media.StopVideo(source); },
       [done = std::move(done), source](MediaResult result) mutable {
         TALK_LOG_INFO("media", "视频已停止 source={} ok={} error={}", source,
                       result.ok, result.error);
         if (done) {
           done(std::move(result));
         }
       },
       false);
}

void MediaController::StartCameraPreview(
    int device_index, std::function<void(MediaResult)> done) {
  TALK_LOG_INFO("media", "启动摄像头预览 device={}", device_index);
  Post([device_index](Media& media) {
         return media.StartCameraPreview(device_index);
       },
       std::move(done));
}

void MediaController::StopCameraPreview(
    std::function<void(MediaResult)> done) {
  TALK_LOG_INFO("media", "停止摄像头预览");
  Post([](Media& media) { return media.StopCameraPreview(); },
       std::move(done), false);
}

void MediaController::GetCameras(std::function<void(MediaResult)> done) {
  TALK_LOG_DEBUG("media", "枚举摄像头");
  Post([](Media& media) { return media.GetCameras(); }, std::move(done));
}

void MediaController::GetDevices(std::function<void(MediaResult)> done) {
  TALK_LOG_DEBUG("media", "枚举音频设备");
  Post([](Media& media) { return media.GetDevices(); }, std::move(done));
}

void MediaController::GetStats(std::function<void(MediaResult)> done) {
  TALK_LOG_DEBUG("media", "读取媒体统计");
  Post([](Media& media) { return media.GetStats(); }, std::move(done));
}

void MediaController::SetVolume(double gain,
                                std::function<void(MediaResult)> done) {
  Post(
      [gain](Media& media) {
        if (!std::isfinite(gain) || gain < 0 || gain > 1) {
          TALK_LOG_WARN("media", "音量越界 gain={}", gain);
          throw std::runtime_error("音量越界");
        }
        media.SetVolume(gain);
        return Json::object();
      },
      std::move(done));
}

void MediaController::SetAudioSource(const std::string& source,
                                     std::function<void(MediaResult)> done) {
  TALK_LOG_INFO("media", "切换采集源 source={}", source);
  // 成功后回写配置（done 在控制线程回调）：失败时保持旧源，
  // 避免重连/重建媒体时用上用户已放弃的源。
  Post([source](Media& media) { return media.SetAudioSource(source); },
       [this, source, done = std::move(done)](MediaResult result) {
         if (result.ok) {
           queue_.SetAudioSourceInConfig(source);
         }
         if (done) {
           done(std::move(result));
         }
       });
}

void MediaController::Post(MediaTaskQueue::Job job,
                           MediaTaskQueue::Done done, bool create) {
  queue_.Post(std::move(job), std::move(done), create);
}

void MediaController::HandleMediaEvent(std::string name, Json data) {
  if (name == "media.state") {
    static const char* const kStates[] = {"new",       "connecting",
                                          "connected", "disconnected",
                                          "failed",    "closed"};
    const int state = data.value("state", -1);
    const char* text = state >= 0 && state < 6 ? kStates[state] : "unknown";
    TALK_LOG_INFO("media", "媒体连接 pc={} state={}",
                  data.value("pc", std::string()), text);
    events_.Emit(name, std::move(data));
    return;
  }
  if (name == "media.path") {
    TALK_LOG_INFO("media", "媒体路径 pc={} path={} 本地={} 远端={}",
                  data.value("pc", std::string()),
                  data.value("path", std::string()),
                  data.value("local", std::string()),
                  data.value("remote", std::string()));
    events_.Emit(name, std::move(data));
    return;
  }
  if (name == "media.track") {
    // 收到远端轨道是订阅成功的第一个实证；随后靠周期统计看 RTP 是否流动。
    TALK_LOG_INFO("media", "收到远端轨道 pc={} kind={} mid={}",
                  data.value("pc", std::string()),
                  data.value("kind", std::string()),
                  data.value("mid", std::string()));
    const std::string mid = data.value("mid", std::string());
    if (data.value("pc", std::string()) == "p2p" &&
        data.value("kind", std::string()) == "video" && !p2p_peer_.empty() &&
        !mid.empty() &&
        p2p_video_sources_.find(mid) == p2p_video_sources_.end()) {
      // P2P m-line 顺序同 SFU（音频/摄像头/屏幕）：先到的是摄像头，后到是屏幕。
      const std::string source =
          p2p_video_sources_.empty() ? "camera" : "screen";
      p2p_video_sources_[mid] = source;
      events_.Emit("video.track",
                   {{"mid", mid}, {"uid", p2p_peer_}, {"source", source}});
    }
    events_.Emit(name, std::move(data));
    return;
  }
  if (name == "p2p.candidate") {
    if (mode_ != Mode::kP2p || p2p_session_.empty()) {
      return;
    }
    Json out = {{"session", p2p_session_},
                {"to", p2p_peer_},
                {"candidate", data.value("candidate", std::string())}};
    if (data.contains("sdp_mid")) {
      out["sdp_mid"] = data["sdp_mid"];
    }
    if (data.contains("sdp_mline_index")) {
      out["sdp_mline_index"] = data["sdp_mline_index"];
    }
    if (signaling_.notify) {
      signaling_.notify("p2p.candidate", std::move(out));
    }
    return;
  }
  if (name == "p2p.state") {
    if (mode_ != Mode::kP2p || p2p_session_.empty()) {
      return;
    }
    const std::string state = data.value("state", std::string());
    Json out = {{"session", p2p_session_}, {"state", state}};
    if (data.contains("path")) {
      out["path"] = data["path"];
    }
    if (signaling_.notify) {
      signaling_.notify("p2p.state", std::move(out));
    }
    if (state == "connected") {
      p2p_started_ = true;
      p2p_connected_ = true;
      TALK_LOG_INFO("media", "P2P 已连通 path={}",
                    data.value("path", std::string()));
      // 新链路已出声：SFU 只静音不关闭，切回时复用同一对 PC 重新协商。
      SetSfuPaused(true);
      StartStatsLogging();
      events_.Emit("published", {{"ok", true}, {"mode", "p2p"}});
    } else if (state == "failed" || state == "closed") {
      TALK_LOG_WARN("media", "P2P 链路断开 state={}", state);
      if (p2p_connected_) {
        // P2P 断了先把 SFU 旧链路恢复出声，等服务端下发 sfu 切换。
        SetSfuPaused(false);
      }
      p2p_connected_ = false;
    }
    events_.Emit(name, data);
    return;
  }
  events_.Emit(name, std::move(data));
}

void MediaController::CloseP2p() {
  TALK_LOG_INFO("media", "关闭 P2P 链路 session={}", p2p_session_);
  ++p2p_epoch_;
  Post([](Media& media) {
         media.CloseP2p();
         return Json::object();
       },
       nullptr, false);
  p2p_session_.clear();
  p2p_peer_.clear();
  p2p_video_sources_.clear();
  p2p_started_ = false;
  p2p_connected_ = false;
}

void MediaController::CompleteSfuSwitch() {
  mode_timer_.cancel();
  if (!on_sfu_ready_) {
    return;
  }
  std::function<void()> ready = std::move(on_sfu_ready_);
  on_sfu_ready_ = nullptr;
  ready();
}

void MediaController::StartStatsLogging() {
  if (stats_logging_ || !queue_.enabled()) {
    return;
  }
  stats_logging_ = true;
  stats_baselines_.clear();
  stats_no_recv_warned_.clear();
  TALK_LOG_INFO("media", "开始音频接收统计 interval=5s mode={}",
                mode_ == Mode::kP2p ? "p2p" : "sfu");
  ScheduleStatsLog();
}

void MediaController::StopStatsLogging() {
  if (!stats_logging_) {
    return;
  }
  stats_logging_ = false;
  stats_timer_.cancel();
  TALK_LOG_DEBUG("media", "停止音频接收统计");
}

void MediaController::ScheduleStatsLog() {
  if (!stats_logging_) {
    return;
  }
  stats_timer_.expires_after(kStatsLogInterval);
  stats_timer_.async_wait([this](const asio::error_code& error) {
    if (error || !stats_logging_) {
      return;
    }
    LogStats();
    ScheduleStatsLog();
  });
}

void MediaController::LogStats() {
  const uint64_t epoch = epoch_;
  // create=false：统计不负责创建媒体实例，避免先于订阅产生副作用。
  Post(
      [](Media& media) { return media.GetStats(); },
      [this, epoch](MediaResult result) {
        if (epoch != epoch_ || !result.ok || !result.data.is_object()) {
          return;
        }
        try {
          for (const char* name : {"recv", "p2p", "send"}) {
            if (result.data.contains(name) && result.data[name].is_object()) {
              LogPeerStats(name, result.data[name]);
            }
          }
          LogVideoCapture(result.data);
          LogDeviceStats(result.data);
        } catch (const std::exception& error) {
          // 统计只是诊断输出，字段类型不对不能把会话线程带崩。
          TALK_LOG_WARN("media", "统计日志异常：{}", error.what());
        }
      },
      false);
}

void MediaController::LogPeerStats(const std::string& name, const Json& stats) {
  if (stats.empty()) {
    return;
  }
  TALK_LOG_INFO(
      "media",
      "音频传输 pc={} dtls={} ice={} 收字节={} 收包={} 发字节={} 发包={}",
      name, stats.value("dtls_state", std::string()),
      stats.value("ice_state", std::string()),
      stats.value("transport_received_bytes", 0ULL),
      stats.value("transport_received_packets", 0ULL),
      stats.value("transport_sent_bytes", 0ULL),
      stats.value("transport_sent_packets", 0ULL));
  const std::string selected_pair =
      stats.value("selected_pair", std::string());
  if (!selected_pair.empty()) {
    TALK_LOG_INFO("media", "音频候选 pc={} {} rtt={:.1f}ms", name,
                  selected_pair, stats.value("rtt_ms", -1.0));
  }
  StatsBaseline& baseline = stats_baselines_[name];
  {
    // 视频统计对 send/recv 都有意义，放在 send 提前 return 之前打印。
    const uint64_t frames = stats.value("video_frames_sent", 0ULL) +
                            stats.value("video_frames_received", 0ULL);
    const uint64_t delta =
        frames >= baseline.video_frames ? frames - baseline.video_frames : 0;
    baseline.video_frames = frames;
    TALK_LOG_INFO(
        "media",
        "视频传输 pc={} 周期帧={} 发帧={} 收帧={} 发字节={} 收字节={} "
        "fps={:.1f} 尺寸={}",
        name, delta, stats.value("video_frames_sent", 0ULL),
        stats.value("video_frames_received", 0ULL),
        stats.value("video_sent_bytes", 0ULL),
        stats.value("video_received_bytes", 0ULL),
        stats.value("video_fps", 0.0),
        stats.value("video_size", std::string("-")));
  }
  if (name == "send") {
    // P2P 期间 SFU send PC 尚未发布，上行统计恒为 0，打印只会误导。
    if (mode_ != Mode::kSfu) {
      return;
    }
    const uint64_t sent = stats.value("sent", 0ULL);
    const uint64_t delta = sent >= baseline.sent ? sent - baseline.sent : 0;
    baseline.sent = sent;
    TALK_LOG_INFO("media", "音频发送 pc={} 周期包={} 累计包={} 字节={}",
                  name, delta, sent, stats.value("sent_bytes", 0ULL));
    return;
  }
  const uint64_t received = stats.value("received", 0ULL);
  const uint64_t samples = stats.value("samples_received", 0ULL);
  const uint64_t delta =
      received >= baseline.received ? received - baseline.received : 0;
  const uint64_t delta_samples =
      samples >= baseline.samples ? samples - baseline.samples : 0;
  baseline.received = received;
  baseline.samples = samples;
  if (received == 0 && stats_no_recv_warned_.insert(name).second) {
    TALK_LOG_WARN("media", "音频接收 pc={} 尚未收到任何 RTP 包", name);
  }
  TALK_LOG_INFO(
      "media", "音频接收 pc={} 周期包={} 累计包={} 字节={} 丢包={} "
                "抖动={:.1f}ms",
      name, delta, received, stats.value("received_bytes", 0ULL),
      stats.value("lost", int64_t{0}),
      stats.value("jitter_seconds", 0.0) * 1000.0);
  TALK_LOG_INFO(
      "media", "音频质量 pc={} 电平={:.3f} 能量={:.3f} 周期样本={} 隐藏={}",
      name, stats.value("audio_level", -1.0),
      stats.value("audio_energy", 0.0), delta_samples,
      stats.value("concealed_samples", 0ULL));
}

void MediaController::LogVideoCapture(const Json& data) {
  if (!data.contains("video_capture") || !data["video_capture"].is_object()) {
    return;
  }
  const Json& capture = data["video_capture"];
  const uint64_t frames = capture.value("frames", 0ULL);
  const bool sharing = capture.value("sharing", false);
  const uint64_t delta =
      frames >= video_capture_baseline_ ? frames - video_capture_baseline_
                                        : 0;
  video_capture_baseline_ = frames;
  // 没有 "send" 段说明当前没有 SFU 上行 PC；有段但未发布时值也恒为 0。
  // 日志参数不做预拼接：局部 std::string 交给日志宏曾有打印为空的前科。
  const uint64_t sent_frames =
      data.contains("send") && data["send"].is_object()
          ? data["send"].value("video_frames_sent", 0ULL)
          : 0ULL;
  const std::string send_state_text =
      data.value("send_state", std::string());
  TALK_LOG_INFO(
      "media", "视频上行 共享中={} 采集帧={} 周期帧={} 已发帧={} pc状态={}",
      sharing, frames, delta, sent_frames,
      mode_ == Mode::kP2p ? "p2p（无 SFU 连接）" : send_state_text.c_str());
  // 采集线程一帧都没出：通常是桌面复制服务未启动或权限问题。
  if (sharing && delta == 0 && !video_capture_warned_) {
    video_capture_warned_ = true;
    TALK_LOG_WARN("media", "共享中但桌面采集本周期未出帧");
  }
}

void MediaController::LogDeviceStats(const Json& data) {
  uint64_t capture_frames = 0;
  if (data.contains("capture") && data["capture"].is_object()) {
    capture_frames = data["capture"].value("frames", 0ULL);
  }
  const Json device = data.contains("device") && data["device"].is_object()
                          ? data["device"]
                          : Json::object();
  const bool recording = device.value("recording", false);
  if (!device.empty()) {
    TALK_LOG_INFO(
        "media",
        "音频设备 采集帧={} 采集中={} 播放中={} 延迟={}ms 播放错误={} "
        "采集口={} 播放口={}",
        capture_frames, recording, device.value("playing", false),
        device.value("playout_delay_ms", 0),
        device.value("playout_error", false),
        device.value("recording_device", std::string()),
        device.value("playout_device", std::string()));
  }
  // 采集源与上行 sender 状态：无声时先区分“设备没采”还是“sender 没开”。
  const Json send_active =
      data.contains("send_active") && data["send_active"].is_object()
          ? data["send_active"]
          : Json::object();
  const bool sender_audio = send_active.value("audio", false);
  const bool sender_video = send_active.value("video", false);
  // 只有 SFU 才有 send PC；P2P 期间 sender 指标不适用，打印成 n/a。
  const bool sfu = mode_ == Mode::kSfu;
  const Json lb = data.contains("switchable") && data["switchable"].is_object()
                      ? data["switchable"].value("loopback", Json::object())
                      : Json::object();
  // 字段类型以实际写入为准：last_error 是 HRESULT 数值，不能当字符串读。
  const long long loopback_error =
      lb.contains("last_error") && lb["last_error"].is_number()
          ? lb["last_error"].get<long long>()
          : 0;
  TALK_LOG_INFO(
      "media",
      "音频上行 采集源={} 静音={} SFU暂停={} 音频sender={} 视频sender={} "
      "录音初始化={} loopback运行={} loopback帧={} loopback静音包={} "
      "loopback错误={}",
      data.value("audio_source", std::string("mic")),
      send_active.value("muted", muted_.load()),
      send_active.value("paused", false),
      sfu ? (sender_audio ? "true" : "false") : "n/a",
      sfu ? (sender_video ? "true" : "false") : "n/a",
      device.value("recording_initialized", false),
      lb.value("running", false), lb.value("frames", 0ULL),
      lb.value("silent_packets", 0ULL), loopback_error);
  const uint64_t capture_delta = capture_frames >= audio_capture_baseline_
                                     ? capture_frames - audio_capture_baseline_
                                     : 0;
  audio_capture_baseline_ = capture_frames;
  // P2P 期间没有 SFU 的 send PC，统计里根本没有 "send" 段，累计发包恒为 0；
  // 这是正常路径，不能当成“已开麦却零上行”报警。
  if (!sfu) {
    audio_idle_cycles_ = 0;
    audio_idle_warned_ = false;
    return;
  }
  const uint64_t sent = data.contains("send") && data["send"].is_object()
                            ? data["send"].value("sent", 0ULL)
                            : 0ULL;
  // sender 未激活：先看静音/暂停标志，再看命令是否失败（失败时状态已置 false）。
  const bool sender_off = !sender_audio;
  if (!muted_.load() && (capture_delta == 0 || sender_off || sent == 0)) {
    ++audio_idle_cycles_;
  } else {
    audio_idle_cycles_ = 0;
    audio_idle_warned_ = false;
  }
  if (audio_idle_cycles_ >= 3 && !audio_idle_warned_) {
    audio_idle_warned_ = true;
    TALK_LOG_WARN(
        "media",
        "已开麦但音频上行异常：采集中={} 周期采集帧={} 音频sender={} "
        "累计发包={} mode={}",
        recording, capture_delta, sender_audio, sent,
        mode_ == Mode::kP2p ? "p2p" : "sfu");
  }
}

void MediaController::EmitMediaError(const std::string& message,
                                     const std::string& detail) {
  TALK_LOG_ERROR("media", "{}：{}", message, detail);
  events_.Emit("media.error", {{"msg", message}, {"detail", detail}});
}

}  // namespace talk
