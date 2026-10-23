// File: core/session/call_session.h
// Brief: 一次信令会话：hello、重连、心跳、入房与协议命令路由。
#ifndef TALK_SESSION_CALL_SESSION_H_
#define TALK_SESSION_CALL_SESSION_H_

#include <chrono>
#include <cstdint>
#include <random>
#include <string>

#include <asio.hpp>

#include "base/event.h"
#include "base/json.h"
#include "media/media_controller.h"
#include "signaling/signaling_client.h"

namespace talk {

// 所有公开方法只在控制线程调用。
class CallSession {
 public:
  CallSession(asio::io_context& io, const Json& config, EventSink& events,
              SignalingClient& signaling, MediaController& media);
  ~CallSession();

  CallSession(const CallSession&) = delete;
  CallSession& operator=(const CallSession&) = delete;

  // 执行一条 talk_command；异常由调用方捕获为参数错误。
  void Command(uint64_t id, std::string method, Json data);

  // 输出 result 事件；id 为 0 表示内部操作。
  void Complete(uint64_t id, const std::string& method, Json result);

  // 停止定时器、终止连接；在 io 线程调用。
  void Shutdown();

 private:
  void HandleAuthUpdate(uint64_t id, const std::string& method, Json data);
  void HandleConnect(uint64_t id, const std::string& method, Json data);
  void HandleDisconnect(uint64_t id, const std::string& method);
  void HandleMediaCommand(uint64_t id, const std::string& method, Json data);
  void HandleJoin(uint64_t id, const std::string& method, Json data);
  void HandleLeave(uint64_t id, const std::string& method);
  void HandleSetMute(uint64_t id, const std::string& method, Json data);
  void HandleVideoStart(uint64_t id, const std::string& method);
  void HandleVideoStop(uint64_t id, const std::string& method);
  void HandleSessionRequest(uint64_t id, const std::string& method,
                            Json data);
  void StartConnect(uint64_t operation, bool recovery);
  void HandleOpen(uint64_t epoch, uint64_t operation, bool recovery);
  void HandleClosed(const SignalingClient::CloseInfo& info,
                    uint64_t operation);
  void HandleMessage(Json message);
  void HandleServerRequest(const std::string& id, const std::string& method,
                           Json data);
  void HandleNotification(const std::string& method, Json data);
  void HandleMediaModeRequest(const std::string& id, Json data);
  // 链路模式变更同步给上层：界面靠它刷新共享屏幕等能力开关。
  void EmitModeChanged(const std::string& mode);
  void HandleSfuOffer(const std::string& id, const Json& data);
  void HandleLost();
  void SchedulePing();
  void ResetMedia();

  asio::io_context& io_;
  Json config_;
  EventSink& events_;
  SignalingClient& signaling_;
  MediaController& media_;
  asio::steady_timer heartbeat_;
  asio::steady_timer reconnect_;
  std::mt19937 random_{std::random_device{}()};
  Json ice_servers_ = Json::array();
  Json join_intent_ = Json::object();
  std::string url_;
  std::string token_;
  std::string session_id_;
  std::string room_id_;
  uint64_t epoch_ = 0;
  uint64_t mute_epoch_ = 0;
  uint64_t video_epoch_ = 0;
  int retry_count_ = 0;
  int heartbeat_seconds_ = 15;
  int resume_seconds_ = 60;
  std::chrono::steady_clock::time_point resume_deadline_{};
  bool stopping_ = false;
  bool ready_ = false;
  bool retry_ = false;
  bool joining_ = false;
  bool disconnecting_ = false;
  bool video_sharing_ = false;
};

}  // namespace talk

#endif  // TALK_SESSION_CALL_SESSION_H_
