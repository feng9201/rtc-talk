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
        const bool resumed = j["data"].contains("resume");
        data = {{"session_id", "test-session"}, {"uid", "self"}, {"resumed", resumed},
                {"heartbeat_interval", 30}, {"resume_ttl", 60}, {"ice_servers", Json::array()}};
        if (resumed && joined) data["room"] = snapshot();
      } else if (method == "room.join") {
        ++joins;
        check(j["data"]["media"]["audio"]["muted"] == true, "初始状态不是静音");
        joined = true; data = snapshot();
      } else if (method == "room.leave") {
        joined = false;
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
  Json snapshot() { return {{"room", {{"room_id", "test-room"}}}, {"peers", Json::array()}}; }
  Server server;
  websocketpp::connection_hdl connection;
  std::thread thread;
  Events events, replies;
  talk_handle* core = nullptr;
  std::string url;
  bool joined = false, delay_unmute = false, drop_ping = false;
  int joins = 0, byes = 0, opens = 0;
  Json delayed;
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
    std::cout << "信令：握手、入房、双向请求、迟到开麦、resume、超时、bye、顶号通过\n";
    return 0;
  } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
