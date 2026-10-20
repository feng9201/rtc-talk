#include "talk/talk.h"
#include "signaling/protocol.h"
#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>
#include <asio.hpp>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <iostream>
#include <future>
#include <mutex>
#include <thread>

using talk::Json;
using namespace std::chrono_literals;
using Server = websocketpp::server<websocketpp::config::asio>;

void check(bool ok, const char* message) {
  if (!ok) throw std::runtime_error(message);
}
struct Events {
  std::mutex mutex;
  std::condition_variable condition;
  std::deque<std::pair<std::string, Json>> items;
  static void callback(void* user, const char* name, const char* text) {
    auto& self = *static_cast<Events*>(user);
    std::lock_guard<std::mutex> lock(self.mutex);
    self.items.emplace_back(name, Json::parse(text));
    self.condition.notify_all();
  }
  Json wait(const std::string& name, uint64_t id = 0, std::chrono::seconds timeout = 5s) {
    std::unique_lock<std::mutex> lock(mutex);
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    for (;;) {
      for (auto it = items.begin(); it != items.end(); ++it) {
        if (it->first == name && (!id || it->second.value("id", uint64_t(0)) == id)) {
          Json result = it->second;
          items.erase(it);
          return result;
        }
      }
      if (condition.wait_until(lock, deadline) == std::cv_status::timeout)
        throw std::runtime_error("等待客户端事件超时：" + name);
    }
  }
  Json wait_str(const std::string& name, const std::string& id, std::chrono::seconds timeout = 5s) {
    std::unique_lock<std::mutex> lock(mutex);
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    for (;;) {
      for (auto it = items.begin(); it != items.end(); ++it) {
        if (it->first == name && it->second.is_object() && it->second.value("id", "") == id) {
          Json result = it->second;
          items.erase(it);
          return result;
        }
      }
      if (condition.wait_until(lock, deadline) == std::cv_status::timeout)
        throw std::runtime_error("等待客户端事件超时：" + name);
    }
  }
};
class Fixture {
 public:
  Fixture() {
    server.clear_access_channels(websocketpp::log::alevel::all);
    server.clear_error_channels(websocketpp::log::elevel::all);
    server.init_asio();
    server.set_validate_handler([this](auto h) {
      auto c = server.get_con_from_hdl(h);
      const bool good = c->get_request_header("Authorization") == "Bearer test-only" &&
                        c->get_requested_subprotocols() == std::vector<std::string>{"talk.v1.json"};
      if (good) c->select_subprotocol("talk.v1.json");
      return good;
    });
    server.set_open_handler([this](auto h) { connection = h; ++opens; });
    server.set_message_handler([this](auto h, Server::message_ptr message) {
      auto j = Json::parse(message->get_payload());
      if (j["type"] == "res") {
        replies.callback(&replies, "server-result", message->get_payload().c_str());
        return;
      }
      if (j["type"] != "req") return;
      const auto method = j["method"].get<std::string>();
      Json data = Json::object();
      if (method == "session.hello") {
        hello = j["data"];
        const bool resumed = j["data"].contains("resume");
        data = {{"session_id", "test-session"}, {"uid", "self"}, {"resumed", resumed},
                {"heartbeat_interval", 30}, {"resume_ttl", 60}, {"ice_servers", Json::array()}};
        if (resumed && joined) data["room"] = snapshot();
      } else if (method == "room.join") {
        ++joins;
        check(j["data"]["media"]["audio"]["muted"] == true, "初始状态不是静音");
        check(j["data"]["policy"].contains("media_mode"), "入房缺少 media_mode");
        join_policy = j["data"]["policy"]["media_mode"].get<std::string>();
        joined = true; data = snapshot();
      } else if (method == "room.leave") {
        joined = false;
      } else if (method == "call.invite") {
        data = {{"call_id", "call-test"}, {"room_id", "test-room"}};
      } else if (method == "media.setMute") {
        if (!j["data"]["muted"].get<bool>() && delay_unmute) { delayed = j; return; }
        data = {{"audio", {{"muted", j["data"]["muted"]}}}};
      } else if (method == "session.bye") {
        ++byes; joined = false;
      } else if (method == "session.ping" && drop_ping) {
        return;
      }
      server.send(h, talk::response(j["id"], data).dump(), websocketpp::frame::opcode::text);
    });
    server.listen(asio::ip::tcp::endpoint(asio::ip::make_address("127.0.0.1"), 0));
    asio::error_code ec;
    const auto port = server.get_local_endpoint(ec).port();
    check(!ec, "读取监听端口失败");
    url = "ws://127.0.0.1:" + std::to_string(port) + "/talk/v1/ws";
    server.start_accept();
    thread = std::thread([this] { server.run(); });
    talk_options options{sizeof(talk_options), TALK_ABI_VERSION, Events::callback, &events, "{}"};
    core = talk_create(&options);
    check(core != nullptr, "创建失败");
  }
  ~Fixture() {
    talk_destroy(core);
    server.stop();
    thread.join();
  }
  uint64_t command(const char* method, Json data = Json::object()) {
    auto id = talk_command(core, method, data.dump().c_str());
    check(id != 0, "命令提交失败");
    return id;
  }
  Json result(uint64_t id, std::chrono::seconds timeout = 5s) { return events.wait("result", id, timeout); }
  void connect() {
    check(result(command("connect", {{"url", url}, {"token", "test-only"}}))["ok"] == true, "连接失败");
    events.wait("ready");
  }
  template<class F> void on_server(F task) {
    std::promise<void> done;
    server.get_io_service().post([&] { try { task(); done.set_value(); } catch (...) { done.set_exception(std::current_exception()); } });
    done.get_future().get();
  }
  Json snapshot() {
    Json out = {{"room", {{"room_id", "test-room"}}}, {"peers", Json::array()},
                {"media_mode", view_media_mode}};
    if (!view_p2p.is_null()) out["p2p"] = view_p2p;
    return out;
  }
  void server_send(const Json& message) {
    on_server([&] { server.send(connection, message.dump(), websocketpp::frame::opcode::text); });
  }
  Server server;
  websocketpp::connection_hdl connection;
  std::thread thread;
  Events events, replies;
  talk_handle* core = nullptr;
  std::string url;
  bool joined = false, delay_unmute = false, drop_ping = false;
  int joins = 0, byes = 0, opens = 0;
  Json delayed;
  Json hello = Json::object();
  std::string join_policy;
  std::string view_media_mode = "sfu";
  Json view_p2p = nullptr;
};
int main() {
  try {
    Fixture f;
    check(!talk_command(nullptr, "x", "{}"), "空句柄必须拒绝");
    check(!talk_command(f.core, "x", "[]"), "非对象参数必须拒绝");
    check(f.result(f.command("room.join"))["ok"] == false, "连接前必须拒绝入房");
    f.connect();
    check(f.result(f.command("room.join", {{"room_id", "test-room"}}))["ok"] == true, "入房失败");
    f.events.wait("joined");
    f.on_server([&] { f.server.send(f.connection, Json({{"v",1},{"type","req"},{"id","s-unknown"},{"method","future.method"}}).dump(), websocketpp::frame::opcode::text); });
    check(f.replies.wait("server-result")["error"]["code"] == 501, "未知服务器请求未回501");
    f.on_server([&] { f.delay_unmute = true; });
    auto unmute = f.command("media.setMute", {{"muted", false}});
    auto mute = f.command("media.setMute", {{"muted", true}});
    check(f.result(mute)["ok"] == true, "静音失败");
    f.on_server([&] { f.server.send(f.connection, talk::response(f.delayed["id"], {{"audio",{{"muted",false}}}}).dump(), websocketpp::frame::opcode::text); });
    check(f.result(unmute)["ok"] == false, "迟到开麦应答不能再作为成功报告");
    f.on_server([&] { f.server.get_con_from_hdl(f.connection)->terminate(websocketpp::lib::error_code()); });
    auto ready = f.events.wait("ready");
    check(ready["resumed"] == true, "未尝试恢复原会话");
    f.events.wait("joined");
    f.on_server([&] { check(f.joins == 1, "恢复快照后重复入房"); f.drop_ping = true; });
    auto ping = f.command("session.ping");
    check(f.result(ping, 12s)["error"]["code"] == 504, "请求未按时超时");
    check(f.result(f.command("disconnect"))["ok"] == true, "断开失败");
    f.on_server([&] { check(f.byes == 1, "正常断开未发送 session.bye"); });
    f.connect();
    f.on_server([&] { f.server.send(f.connection, Json({{"v",1},{"type","ntf"},{"method","session.kicked"},{"data",{{"reason","replaced"}}}}).dump(), websocketpp::frame::opcode::text); });
    f.events.wait("session.kicked");
    std::this_thread::sleep_for(1500ms);
    f.on_server([&] { check(f.opens == 3, "顶号后不应自动重新连接"); });

    {
      Fixture g;
      g.connect();
      check(g.result(g.command("room.join", {{"room_id", "p2p-room"}}))["ok"] == true, "S3 入房失败");
      g.events.wait("joined");
      g.on_server([&] {
        check(g.hello["caps"]["p2p"] == true, "hello 未声明 p2p 能力");
        check(g.join_policy == "auto", "入房未使用 auto 模式");
      });

      // 服务端要求进入 P2P：先应答，再处理媒体。
      g.server_send({{"v",1},{"type","req"},{"id","s-p2p"},{"method","room.mediaMode"},
                     {"data",{{"mode","p2p"},{"reason","peer_count"},
                              {"p2p",{{"role","offerer"},{"peer","bob"},{"session","sess-1"}}}}}});
      check(g.replies.wait_str("server-result", "s-p2p")["ok"] == true, "room.mediaMode p2p 未成功应答");
      check(g.events.wait("mediaMode")["media_mode"] == "p2p", "未报告 p2p 模式");

      // 无媒体后端时不建链，但 P2P 通知不能破坏会话。
      g.server_send({{"v",1},{"type","ntf"},{"method","p2p.answer"},
                     {"data",{{"session","sess-1"},{"from","bob"},{"sdp","v=0"}}}});
      g.server_send({{"v",1},{"type","ntf"},{"method","p2p.candidate"},
                     {"data",{{"session","sess-1"},{"from","bob"},
                               {"candidate","candidate:1 1 UDP 1 127.0.0.1 9000 typ host"},
                               {"sdp_mid","0"},{"sdp_mline_index",0}}}});
      check(g.result(g.command("session.ping"))["ok"] == true, "P2P 通知后会话不可用");

      g.server_send({{"v",1},{"type","ntf"},{"method","p2p.close"},{"data",{{"session","sess-1"}}}});
      check(g.events.wait("p2p.closed")["session"] == "sess-1", "未处理 p2p.close");
      check(g.events.wait("mediaMode")["media_mode"] == "sfu", "p2p.close 后未回到 sfu");

      // 切回 SFU：无媒体后端时立即应答。
      g.server_send({{"v",1},{"type","req"},{"id","s-sfu"},{"method","room.mediaMode"},
                     {"data",{{"mode","sfu"},{"reason","peer_count"},{"media_mode","sfu"}}}});
      check(g.replies.wait_str("server-result", "s-sfu")["ok"] == true, "room.mediaMode sfu 未成功应答");

      // call.* 命令与通知透传。
      auto invited = g.result(g.command("call.invite", {{"callees", Json::array({"bob"})}, {"room_id", "p2p-room"}}));
      check(invited["ok"] == true && invited["data"]["call_id"] == "call-test", "call.invite 未透传");
      g.server_send({{"v",1},{"type","ntf"},{"method","call.incoming"},
                     {"data",{{"call_id","call-test"},{"room_id","p2p-room"},{"from","bob"}}}});
      check(g.events.wait("call.incoming")["call_id"] == "call-test", "call.incoming 未上报");
      g.server_send({{"v",1},{"type","ntf"},{"method","call.state"},
                     {"data",{{"call_id","call-test"},{"state","accepted"}}}});
      check(g.events.wait("call.state")["state"] == "accepted", "call.state 未上报");

      // resume 快照带 P2P 模式时不重发入房。
      g.on_server([&] {
        g.view_media_mode = "p2p";
        g.view_p2p = {{"role","offerer"},{"peer","bob"},{"session","sess-1"}};
        websocketpp::lib::error_code ec;
        g.server.get_con_from_hdl(g.connection)->terminate(ec);
      });
      check(g.events.wait("ready")["resumed"] == true, "S3 resume 未恢复");
      check(g.events.wait("joined")["media_mode"] == "p2p", "resume 快照未恢复 p2p 模式");
      check(g.result(g.command("disconnect"))["ok"] == true, "S3 断开失败");
    }

    std::cout << "信令：握手、入房、双向请求、迟到开麦、resume、超时、bye、顶号、S3 模式与呼叫通过\n";
    return 0;
  } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
