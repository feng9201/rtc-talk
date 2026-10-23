// File: core/media/media_controller.h
// Brief: 媒体会话编排：SFU 发布、P2P 链路与服务端模式切换。
#ifndef TALK_MEDIA_MEDIA_CONTROLLER_H_
#define TALK_MEDIA_MEDIA_CONTROLLER_H_

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <string>

#include <asio.hpp>

#include "base/event.h"
#include "base/json.h"
#include "media/media_task_queue.h"

namespace talk {

// 媒体层需要的信令能力；由会话层注入，避免控制器直接依赖 WebSocket。
struct MediaSignaling {
  std::function<void(std::string, Json)> notify;
  std::function<void(std::string, Json, std::function<void(Json)>)> request;
};

// 所有公开方法只在控制线程调用；媒体任务在队列线程串行执行。
class MediaController {
 public:
  enum class Mode {
    kSfu,
    kP2p,
  };

  MediaController(asio::io_context& io, const Json& config, EventSink& events,
                  VideoFrameSink video_sink);
  ~MediaController();

  MediaController(const MediaController&) = delete;
  MediaController& operator=(const MediaController&) = delete;

  void SetSignaling(MediaSignaling signaling);
  void SetIceServers(const Json& ice_servers);

  // 会话重置：丢弃媒体实例、P2P 链路与模式状态。
  void Reset();

  // 断线重连：丢弃在途任务结果，但保留媒体实例以便 resume。
  void Invalidate();

  // 析构前调用：等待媒体线程退出。
  void Shutdown();

  bool enabled() const { return queue_.enabled(); }
  Mode mode() const { return mode_; }
  bool muted() const { return muted_.load(); }
  const std::string& p2p_session() const { return p2p_session_; }
  bool p2p_started() const { return p2p_started_; }
  const Json& consumers() const { return consumers_; }
  MuteGate gate() const { return queue_.gate(); }

  // 应用服务端下发的房间视图（media_mode / p2p 分配）。
  void ApplyModeView(const Json& view);

  // SFU：生成并发布 offer；应用服务端下发的订阅 offer。
  void Publish();
  void ApplySfuOffer(const std::string& sdp, const Json& consumers,
                     std::function<void(MediaResult)> done);

  // P2P：按分配建立链路，或响应对端 offer。
  void StartP2p();
  void SwitchToP2p(const Json& allocation);
  // 收到未知会话的 offer 时，以对端参数接管 P2P 会话。
  void AdoptP2pSession(const std::string& session, const std::string& peer);
  void AnswerP2p(const std::string& session, const std::string& from,
                 const std::string& sdp);
  void AcceptP2pAnswer(const std::string& session, const std::string& sdp);
  void AddP2pCandidate(const std::string& session,
                       const std::string& candidate, const std::string& mid,
                       int index);
  void HandleP2pClose(const Json& data);
  // 关闭 P2P PeerConnection 并清空链路状态。
  void CloseP2p();

  // 服务端 room.mediaMode=sfu：新链路出声后（或 2 秒超时）回调。
  void SwitchToSfu(std::function<void()> ready);
  void FinishModeSwitch();

  // 设备、音量与静音；done 可为空。
  void SetSfuPaused(bool paused, std::function<void()> done = {});
  void SetMuted(bool muted, std::function<void(MediaResult)> done = {});
  // 视频启停：source 为 camera/screen；done 可为空，且只在控制线程回调。
  void StartVideo(const std::string& source,
                  std::function<void(MediaResult)> done);
  void StopVideo(const std::string& source,
                 std::function<void(MediaResult)> done = {});
  void GetDevices(std::function<void(MediaResult)> done);
  void GetStats(std::function<void(MediaResult)> done);
  void SetVolume(double gain, std::function<void(MediaResult)> done);
  // 运行时切换采集源；done 可为空。
  void SetAudioSource(const std::string& source,
                      std::function<void(MediaResult)> done);

 private:
  void Post(MediaTaskQueue::Job job, MediaTaskQueue::Done done,
            bool create = true);
  void HandleMediaEvent(std::string name, Json data);
  void EmitMediaError(const std::string& message, const std::string& detail);
  void CompleteSfuSwitch();

  // 周期读取 RTP/设备统计，用于判断远端音频是否真正到达。
  void StartStatsLogging();
  void StopStatsLogging();
  void ScheduleStatsLog();
  void LogStats();
  void LogPeerStats(const std::string& name, const Json& stats);
  // 桌面采集与上行帧数；用来区分“没出帧”和“没发出去”。
  void LogVideoCapture(const Json& data);
  void LogDeviceStats(const Json& data);

  asio::io_context& io_;
  EventSink& events_;
  MediaTaskQueue queue_;
  MediaSignaling signaling_;
  asio::steady_timer mode_timer_;
  asio::steady_timer stats_timer_;

  Mode mode_ = Mode::kSfu;
  std::string p2p_session_;
  std::string p2p_peer_;
  bool p2p_offerer_ = false;
  bool p2p_started_ = false;
  bool p2p_connected_ = false;
  uint64_t epoch_ = 0;
  uint64_t p2p_epoch_ = 0;
  uint64_t switch_seq_ = 0;
  std::function<void()> on_sfu_ready_;
  Json consumers_ = Json::object();

  struct StatsBaseline {
    uint64_t received = 0;
    uint64_t sent = 0;
    uint64_t samples = 0;
    uint64_t video_frames = 0;
  };
  bool stats_logging_ = false;
  std::map<std::string, StatsBaseline> stats_baselines_;
  std::set<std::string> stats_no_recv_warned_;
  // 采集帧计数基线，以及“共享中却零出帧”只报一次的标记。
  uint64_t video_capture_baseline_ = 0;
  bool video_capture_warned_ = false;
  // 本地静音目标；用来判定“开了麦却没声音”是否异常。
  std::atomic<bool> muted_{true};
  // 音频采集帧基线与连续零采集周期数；同一轮只报一次 WARN。
  uint64_t audio_capture_baseline_ = 0;
  uint32_t audio_idle_cycles_ = 0;
  bool audio_idle_warned_ = false;
};

}  // namespace talk

#endif  // TALK_MEDIA_MEDIA_CONTROLLER_H_
