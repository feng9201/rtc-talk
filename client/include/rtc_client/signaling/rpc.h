#pragma once
#include "rtc_client/protocol/message.h"
#include <chrono>
#include <deque>
#include <functional>
#include <map>

namespace rtc_client {
struct RpcResult {
  bool ok{};
  int code{};
  protocol::Json data = protocol::Json::object();
};
struct ServerRequest {
  std::uint64_t generation{}, serial{};
  std::string id, method;
  protocol::Json data;
};

// All calls confined to the control executor. Message bodies remain INTERNAL:
// callbacks must never dump SDP/ICE/auth data into UI events or logs.
class RpcPeer final {
 public:
  using Time = std::chrono::steady_clock::time_point;
  using Send = std::function<bool(const std::string&)>;
  using Done = std::function<void(RpcResult)>;
  using RequestHandler = std::function<void(const ServerRequest&)>;
  using NotificationHandler = std::function<void(const protocol::Message&)>;
  explicit RpcPeer(Send send, std::size_t capacity = 64);
  void set_handlers(RequestHandler, NotificationHandler);
  std::string begin(std::string method, const protocol::Json&, Done, Time now,
                    std::chrono::milliseconds timeout = std::chrono::seconds(10));
  void receive(const protocol::Message&, Time now);
  bool respond(const ServerRequest&, const RpcResult&, Time now);
  void tick(Time now);
  void reset(std::uint64_t generation);
  void close();
  std::size_t pending() const noexcept { return outgoing_.size(); }

 private:
  struct Outgoing { std::string method; Time started, deadline; Done done; };
  struct Incoming { ServerRequest request; Time deadline; };
  bool send(const std::string&) noexcept;
  void invoke(Done, RpcResult) noexcept;
  void cancel_outgoing(int code);
  void remember(std::string id, std::string response);
  Send send_;
  const std::size_t capacity_;
  std::uint64_t generation_{1}, sequence_{}, serial_{}, transition_{};
  bool closed_{false};
  std::map<std::string, Outgoing> outgoing_;
  std::map<std::string, Incoming> incoming_;
  std::map<std::string, std::string> answered_;
  std::deque<std::string> answer_order_;
  RequestHandler request_handler_;
  NotificationHandler notification_handler_;
};
}  // namespace rtc_client
