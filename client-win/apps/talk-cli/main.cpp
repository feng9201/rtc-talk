// File: apps/talk-cli/main.cpp
// Brief: 命令行联调工具：连接测试服务端、入房、采集统计并退出。
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "log_setup.h"
#include "talk/talk.h"

namespace {

using Json = nlohmann::json;
using namespace std::chrono_literals;

// 命令行参数与配置。
struct Options {
  std::string url;
  std::string room;
  std::string token_file;
  std::string config_file;
  bool token_stdin = false;
  bool list_devices = false;
  bool version = false;
  bool video = false;
  // video.start 的采集源：screen（屏幕共享）或 camera（摄像头）。
  std::string video_source = "screen";
  int duration = 10;
  // 不自动开麦：用来还原 Qt 界面的真实命令序列（界面只在用户点「开麦」时发 setMute）。
  bool no_auto_unmute = false;
  // 运行时开麦时刻：0 表示不主动发 media.setMute。
  int unmute_at = 0;
  // 摄像头启停时刻：0 表示不操作，用来验证反复开关的完整链路。
  int video_stop_at = 0;
  int video_restart_at = 0;
  // 运行时切换采集源：<第 N 秒, mic|system>，可多次。
  std::vector<std::pair<int, std::string>> source_switches;
  talk::LogOptions log;
  bool log_file_set = false;
  bool log_level_set = false;
  bool log_console_set = false;
  Json config = {{"audio_backend", "none"}};
};

const char* const kUsage =
    "用法：talk-cli --url ws://主机:端口/talk/v1/ws --room 房间 "
    "--token-file 文件（或 --token-stdin） "
    "[--audio none|fake|wasapi] [--audio-source mic|system] "
    "[--media-mode auto|sfu|p2p] "
    "[--config 文件] [--duration 秒] [--switch-source-at 秒:mic|system] "
    "[--unmute-at 秒] [--no-auto-unmute] "
    "[--video] [--video-source screen|camera] "
    "[--video-stop-at 秒] [--video-restart-at 秒] "
    "[--log-file 路径] [--log-level trace|debug|info|warn|error] "
    "[--log-console]；单跑 --list-devices 枚举音频设备";

Options ParseOptions(int argc, char** argv) {
  Options options;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--version") {
      options.version = true;
      continue;
    }
    if (arg == "--token-stdin") {
      options.token_stdin = true;
      continue;
    }
    if (arg == "--list-devices") {
      options.list_devices = true;
      continue;
    }
    if (arg == "--log-console") {
      options.log.console = true;
      options.log_console_set = true;
      continue;
    }
    if (arg == "--video") {
      options.video = true;
      continue;
    }
    if (arg == "--no-auto-unmute") {
      options.no_auto_unmute = true;
      continue;
    }
    if (i + 1 == argc) {
      throw std::runtime_error("参数缺少值");
    }
    const std::string value = argv[++i];
    if (arg == "--url") {
      options.url = value;
    } else if (arg == "--room") {
      options.room = value;
    } else if (arg == "--token-file") {
      options.token_file = value;
    } else if (arg == "--audio") {
      options.config["audio_backend"] = value;
    } else if (arg == "--audio-source") {
      options.config["audio_source"] = value;
    } else if (arg == "--video-source") {
      options.video_source = value;
    } else if (arg == "--media-mode") {
      options.config["media_mode"] = value;
    } else if (arg == "--config") {
      options.config_file = value;
    } else if (arg == "--duration") {
      options.duration = std::stoi(value);
    } else if (arg == "--unmute-at") {
      options.unmute_at = std::stoi(value);
    } else if (arg == "--video-stop-at") {
      options.video_stop_at = std::stoi(value);
    } else if (arg == "--video-restart-at") {
      options.video_restart_at = std::stoi(value);
    } else if (arg == "--switch-source-at") {
      const size_t sep = value.find(':');
      if (sep == std::string::npos) {
        throw std::runtime_error("switch-source-at 格式为 秒:mic|system");
      }
      options.source_switches.emplace_back(std::stoi(value.substr(0, sep)),
                                           value.substr(sep + 1));
    } else if (arg == "--log-file") {
      options.log.file = value;
      options.log_file_set = true;
    } else if (arg == "--log-level") {
      options.log.level = value;
      options.log_level_set = true;
    } else {
      throw std::runtime_error("未知参数");
    }
  }
  if (!options.config_file.empty()) {
    std::ifstream input(options.config_file);
    input >> options.config;
  }
  if (!options.log_file_set && options.config.contains("log_file") &&
      options.config["log_file"].is_string()) {
    options.log.file = options.config["log_file"].get<std::string>();
  }
  if (!options.log_level_set && options.config.contains("log_level") &&
      options.config["log_level"].is_string()) {
    options.log.level = options.config["log_level"].get<std::string>();
  }
  if (!options.log_console_set && options.config.contains("log_console") &&
      options.config["log_console"].is_boolean()) {
    options.log.console = options.config["log_console"].get<bool>();
  }
  return options;
}

std::string ReadToken(const Options& options) {
  std::string token;
  if (options.token_stdin) {
    std::getline(std::cin, token);
    return token;
  }
  std::ifstream input(options.token_file);
  if (!input) {
    throw std::runtime_error("token 文件不存在或不可读");
  }
  std::getline(input, token);
  return token;
}

void Validate(const Options& options) {
  if (options.url.empty() || options.room.empty() || options.duration < 1 ||
      options.duration > 86400 ||
      options.token_stdin == !options.token_file.empty()) {
    throw std::runtime_error(kUsage);
  }
  if (options.config.contains("media_mode")) {
    const std::string mode = options.config["media_mode"].get<std::string>();
    if (mode != "auto" && mode != "sfu" && mode != "p2p") {
      throw std::runtime_error("media_mode 必须是 auto/sfu/p2p");
    }
  }
  if (options.config.contains("audio_source")) {
    if (!options.config["audio_source"].is_string()) {
      throw std::runtime_error("audio_source 必须是字符串");
    }
    const std::string source =
        options.config["audio_source"].get<std::string>();
    if (source != "mic" && source != "system") {
      throw std::runtime_error("audio_source 必须是 mic/system");
    }
  }
  if (options.unmute_at < 0 || options.unmute_at > options.duration) {
    throw std::runtime_error("unmute-at 秒必须在 0..duration 之间");
  }
  if (options.video_source != "screen" && options.video_source != "camera") {
    throw std::runtime_error("video-source 必须是 screen/camera");
  }
  for (const int at : {options.video_stop_at, options.video_restart_at}) {
    if (at < 0 || at > options.duration) {
      throw std::runtime_error("video-stop-at/restart-at 秒必须在 0..duration 之间");
    }
  }
  if (options.video_restart_at > 0 &&
      options.video_restart_at <= options.video_stop_at) {
    throw std::runtime_error("video-restart-at 必须大于 video-stop-at");
  }
  for (const auto& [sec, source] : options.source_switches) {
    if (sec < 1 || sec > options.duration) {
      throw std::runtime_error("switch-source-at 秒必须在 duration 范围内");
    }
    if (source != "mic" && source != "system") {
      throw std::runtime_error("switch-source-at 源必须是 mic/system");
    }
  }
}

// 事件状态机；回调在 core 线程，只通过原子量暴露给主线程。
class App {
 public:
  explicit App(std::string room) : room_(std::move(room)) {}
  ~App() {
    if (core_ != nullptr) {
      talk_destroy(core_);
    }
  }

  bool Start(const Json& config) {
    if (core_ != nullptr) {
      talk_destroy(core_);
      core_ = nullptr;
    }
    const std::string text = config.dump();
    talk_options options{sizeof(talk_options), TALK_ABI_VERSION, &App::OnEvent,
                         this, text.c_str()};
    core_ = talk_create(&options);
    if (core_ != nullptr) {
      talk_set_video_frame_callback(core_, &App::OnVideoFrame, this);
    }
    return core_ != nullptr;
  }

  void SetVideo(bool video) { video_ = video; }
  bool video() const { return video_; }

  void SetVideoSource(std::string source) { video_source_ = std::move(source); }

  void SetAudio(bool audio) { audio_ = audio; }
  bool audio() const { return audio_; }

  // 关闭自动开麦后，只有 --unmute-at 到点才发 media.setMute。
  void SetNoAutoUnmute(bool no_auto_unmute) {
    no_auto_unmute_ = no_auto_unmute;
  }

  void Command(const char* method, const Json& data = Json::object()) {
    if (core_ == nullptr ||
        talk_command(core_, method, data.dump().c_str()) == 0) {
      failed_ = true;
    }
  }

  bool WaitFor(std::chrono::seconds timeout,
               const std::function<bool()>& done) {
    std::unique_lock<std::mutex> lock(mutex_);
    return condition_.wait_for(lock, timeout, done);
  }

  bool failed() const { return failed_; }
  bool joined() const { return joined_; }
  bool published() const { return published_; }
  bool left() const { return left_; }
  bool disconnected() const { return disconnected_; }
  bool devices_done() const { return devices_done_; }
  bool stats_pending() const { return stats_pending_; }
  void set_stats_pending(bool pending) { stats_pending_ = pending; }
  void Fail() { failed_ = true; }

 private:
  static void OnEvent(void* context, const char* name, const char* text) {
    auto& self = *static_cast<App*>(context);
    try {
      const std::string type(name);
      const Json data = Json::parse(text);
      std::cout << Json({{"event", type}, {"data", data}}).dump()
                << std::endl;
      self.HandleEvent(type, data);
    } catch (...) {
      self.failed_ = true;
    }
    self.condition_.notify_all();
  }

  // 媒体线程每帧回调：按秒节流打印，避免刷屏影响 15fps 链路。
  static void OnVideoFrame(void* context, const char* mid, const uint8_t* bgra,
                           int width, int height, int stride,
                           int64_t timestamp_us) {
    auto& self = *static_cast<App*>(context);
    if (bgra == nullptr || width <= 0 || height <= 0) {
      return;
    }
    const auto now = std::chrono::steady_clock::now();
    if (self.last_frame_.time_since_epoch().count() != 0 &&
        now - self.last_frame_ < 1s) {
      return;
    }
    self.last_frame_ = now;
    const int frames = self.video_frames_.fetch_add(1) + 1;
    std::cout << "远端视频 mid=" << (mid != nullptr ? mid : "") << " "
              << width << "x" << height << " stride=" << stride
              << " 累计帧=" << frames << std::endl;
  }

  void HandleEvent(const std::string& name, const Json& data) {
    if (name == "ready" && !data.value("recovering", false)) {
      Command("room.join", {{"room_id", room_}});
    }
    if (name == "joined") {
      joined_ = true;
      if (!audio_ && !no_auto_unmute_) {
        Command("media.setMute", {{"muted", false}});
      }
    }
    if (name == "published") {
      published_ = true;
      if (!no_auto_unmute_) {
        Command("media.setMute", {{"muted", false}});
      }
      if (video_) {
        Command("video.start", {{"source", video_source_}});
      }
    }
    if (name == "left") {
      left_ = true;
    }
    if (name == "result" &&
        data.value("method", std::string()) == "media.stats") {
      stats_pending_ = false;
    }
    if (name == "result" &&
        data.value("method", std::string()) == "audio.devices") {
      devices_done_ = true;
    }
    if (name == "result" &&
        data.value("method", std::string()) == "disconnect") {
      disconnected_ = true;
    }
    if (name == "media.error" || name == "error" ||
        (name == "result" && !data.value("ok", false))) {
      failed_ = true;
    }
  }

  talk_handle* core_ = nullptr;
  std::string room_;
  bool audio_ = false;
  bool video_ = false;
  std::string video_source_ = "screen";
  bool no_auto_unmute_ = false;
  std::atomic<int> video_frames_{0};
  std::chrono::steady_clock::time_point last_frame_{};
  std::atomic<bool> joined_{false};
  std::atomic<bool> published_{false};
  std::atomic<bool> failed_{false};
  std::atomic<bool> left_{false};
  std::atomic<bool> disconnected_{false};
  std::atomic<bool> stats_pending_{false};
  std::atomic<bool> devices_done_{false};
  std::mutex mutex_;
  std::condition_variable condition_;
};

int ListDevices(Options options) {
  if (options.config.value("audio_backend", std::string("none")) == "none") {
    options.config["audio_backend"] = "wasapi";
  }
  App app{std::string()};
  if (!app.Start(options.config)) {
    throw std::runtime_error("创建 core 失败");
  }
  app.Command("audio.devices");
  app.WaitFor(20s, [&app] {
    return app.devices_done() || app.failed();
  });
  return app.failed() ? 1 : 0;
}

int RunTalk(const Options& options) {
  std::string token = ReadToken(options);
  if (token.empty()) {
    throw std::runtime_error("token 为空");
  }
  Json config = options.config;
  App app(options.room);
  app.SetAudio(config.value("audio_backend", std::string("none")) != "none");
  app.SetNoAutoUnmute(options.no_auto_unmute);
  app.SetVideo(options.video);
  app.SetVideoSource(options.video_source);
  if (!app.Start(config)) {
    throw std::runtime_error("创建 core 失败");
  }
  app.Command("connect", {{"url", options.url}, {"token", token}});
  token.clear();
  app.WaitFor(25s, [&app] {
    return app.failed() || (app.joined() && (!app.audio() || app.published()));
  });
  if (!app.joined() || (app.audio() && !app.published())) {
    app.Fail();
  }
  for (int i = 0; i < options.duration && !app.failed(); ++i) {
    std::this_thread::sleep_for(1s);
    for (const auto& [sec, source] : options.source_switches) {
      if (i + 1 == sec) {
        std::cout << "切换采集源 -> " << source << std::endl;
        app.Command("audio.setSource", {{"source", source}});
      }
    }
    if (options.unmute_at > 0 && i + 1 == options.unmute_at) {
      std::cout << "开麦" << std::endl;
      app.Command("media.setMute", {{"muted", false}});
    }
    if (options.video_stop_at > 0 && i + 1 == options.video_stop_at) {
      std::cout << "关闭视频 source=" << options.video_source << std::endl;
      app.Command("video.stop", {{"source", options.video_source}});
    }
    if (options.video_restart_at > 0 && i + 1 == options.video_restart_at) {
      std::cout << "重新开启视频 source=" << options.video_source << std::endl;
      app.Command("video.start", {{"source", options.video_source}});
    }
    if (app.audio() && (i % 5 == 0 || i == options.duration - 1) &&
        !app.stats_pending()) {
      app.set_stats_pending(true);
      app.Command("media.stats", Json::object());
    }
  }
  if (app.stats_pending()) {
    if (!app.WaitFor(10s, [&app] { return !app.stats_pending(); })) {
      app.Fail();
    }
  }
  if (app.joined()) {
    app.Command("room.leave", Json::object());
    app.WaitFor(3s, [&app] { return app.left(); });
  }
  app.Command("disconnect", Json::object());
  if (!app.WaitFor(11s, [&app] { return app.disconnected(); })) {
    app.Fail();
  }
  return app.failed() ? 1 : 0;
}

}  // namespace

int main(int argc, char** argv) {
  try {
    Options options = ParseOptions(argc, argv);
    if (options.version) {
      std::cout << talk_version() << '\n';
      return 0;
    }
    talk::LoggingGuard logging(options.log);
    if (logging.ok()) {
      std::cerr << "日志：" << logging.path() << '\n';
    } else {
      std::cerr << "日志初始化失败\n";
    }
    if (options.list_devices) {
      return ListDevices(std::move(options));
    }
    Validate(options);
    return RunTalk(options);
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 2;
  }
}
