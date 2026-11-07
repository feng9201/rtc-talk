#include "rtc_client/signaling/rpc.h"
#include "rtc_client/logging/logger.h"
#include <limits>
#include <utility>
#include <vector>

namespace rtc_client {
namespace { RpcResult error(int code) { return {false, code, protocol::Json::object()}; } }
RpcPeer::RpcPeer(Send send, std::size_t capacity) : send_(std::move(send)), capacity_(capacity) {}
void RpcPeer::set_handlers(RequestHandler request, NotificationHandler notification) {
  request_handler_ = std::move(request);
  notification_handler_ = std::move(notification);
}
bool RpcPeer::send(const std::string& text) noexcept {
  try { return send_ && send_(text); } catch (...) { return false; }
}
void RpcPeer::invoke(Done done, RpcResult result) noexcept {
  try { if (done) done(std::move(result)); }
  catch (...) { logging::emit(logging::Level::error,"rpc.callback_failed"); }
}
std::string RpcPeer::begin(std::string method, const protocol::Json& data, Done done,
                           Time now, std::chrono::milliseconds timeout) {
  if (closed_ || outgoing_.size() >= capacity_ || !done || timeout.count() <= 0 ||
      timeout > std::chrono::seconds(60) || sequence_ == std::numeric_limits<std::uint64_t>::max()) return {};
  const auto id = "c-" + std::to_string(generation_) + "-" + std::to_string(++sequence_);
  const auto wire = protocol::request(id, method, data);  // Invalid => NOT accepted.
  logging::emit(logging::Level::info,"rpc.request",{{"generation",generation_},
    {"operation",sequence_},{"method",method},{"pending",outgoing_.size()+1}});
  outgoing_.emplace(id, Outgoing{std::move(method),now,now + timeout,std::move(done)});
  if (!send(wire)) {
    auto it = outgoing_.find(id);
    // send port may deliver a response synchronously in a test or adapter.
    if (it != outgoing_.end()) {
      auto callback = std::move(it->second.done);
      outgoing_.erase(it);
      invoke(std::move(callback), error(480));
    }
  }
  return id;
}
void RpcPeer::remember(std::string id, std::string response) {
  if (answered_.size() >= capacity_ && !answer_order_.empty()) {
    answered_.erase(answer_order_.front());
    answer_order_.pop_front();
  }
  answer_order_.push_back(id);
  answered_.emplace(std::move(id), std::move(response));
}
void RpcPeer::receive(const protocol::Message& message, Time now) {
  if (closed_) return;
  if (message.kind == protocol::Kind::response) {
    auto it = outgoing_.find(message.id);
    if (it == outgoing_.end()) return;
    auto callback = std::move(it->second.done);
    const bool expired = now >= it->second.deadline;
    logging::emit(logging::Level::info,"rpc.response",{{"generation",generation_},
      {"method",it->second.method},{"ok",message.ok && !expired},
      {"code",expired ? 504 : message.error_code},
      {"elapsed_ms",std::chrono::duration_cast<std::chrono::milliseconds>(now-it->second.started).count()}});
    outgoing_.erase(it);
    invoke(std::move(callback), expired ? error(504) : RpcResult{message.ok,message.error_code,message.data});
  } else if (message.kind == protocol::Kind::notification) {
    try { if (notification_handler_) notification_handler_(message); }
    catch (...) { logging::emit(logging::Level::error,"rpc.notification_failed"); }
  } else {
    if (auto old = answered_.find(message.id); old != answered_.end()) { send(old->second); return; }
    if (incoming_.count(message.id)) return;  // A duplicate never runs the handler twice.
    if (incoming_.size() >= capacity_) { send(protocol::failure(message.id,429)); return; }
    if (serial_ == std::numeric_limits<std::uint64_t>::max()) { close(); return; }
    ServerRequest request{generation_,++serial_,message.id,message.method,message.data};
    incoming_.emplace(message.id, Incoming{request,now + std::chrono::seconds(15)});
    try {
      if (request_handler_) request_handler_(request);
      else respond(request,error(501),now);
    } catch (...) { respond(request,error(500),now); }
  }
}
bool RpcPeer::respond(const ServerRequest& request, const RpcResult& result, Time now) {
  if (closed_ || request.generation != generation_) return false;
  auto it = incoming_.find(request.id);
  if (it == incoming_.end() || it->second.request.serial != request.serial) return false;
  const auto wire = now >= it->second.deadline ? protocol::failure(request.id,504)
                    : result.ok ? protocol::success(request.id,result.data)
                                : protocol::failure(request.id,result.code);
  incoming_.erase(it);
  remember(request.id,wire);
  return send(wire);
}
void RpcPeer::tick(Time now) {
  std::vector<std::pair<Done,RpcResult>> results;
  for (auto it = outgoing_.begin(); it != outgoing_.end();) {
    if (it->second.deadline <= now) {
      logging::emit(logging::Level::warning,"rpc.timeout",{{"generation",generation_},
        {"method",it->second.method},{"code",504}});
      results.emplace_back(std::move(it->second.done),error(504));
      it = outgoing_.erase(it);
    } else ++it;
  }
  std::vector<ServerRequest> expired;
  for (const auto& entry : incoming_) if (entry.second.deadline <= now) expired.push_back(entry.second.request);
  for (const auto& request : expired) respond(request,error(504),now);
  for (auto& result : results) invoke(std::move(result.first),std::move(result.second));
}
void RpcPeer::cancel_outgoing(int code) {
  auto old = std::move(outgoing_);
  outgoing_.clear();
  for (auto& entry : old) invoke(std::move(entry.second.done),error(code));
}
void RpcPeer::reset(std::uint64_t generation) {
  if (generation <= generation_) return;
  const auto transition = ++transition_;
  generation_ = generation;
  closed_ = true;  // A cancelled callback cannot enqueue into the new connection.
  incoming_.clear(); answered_.clear(); answer_order_.clear();
  cancel_outgoing(487);
  if (transition == transition_) closed_ = false;
}
void RpcPeer::close() {
  ++transition_;
  closed_ = true;
  incoming_.clear(); answered_.clear(); answer_order_.clear();
  cancel_outgoing(487);
}
}  // namespace rtc_client
