// File: tests/signaling_test.cpp
// Brief: 用本地信令服务器验证连接、入房、双向请求与恢复流程。
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <future>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <asio.hpp>
#include <websocketpp/config/asio_no_tls.hpp>
#include <websocketpp/server.hpp>

#include "signaling/protocol.h"
#include "talk/talk.h"

namespace {

using talk::Json;
using namespace std::chrono_literals;
using Server = websocketpp::server<websocketpp::config::asio>;

void Check(bool ok, const char* message) {
  if (!ok) {
    throw std::runtime_error(message);
  }
}

// 收集核心库事件，供测试线程按名称与操作 ID 等待。
struct Events {
  static void Callback(void* user, const char* name, const char* text) {
    auto& self = *static_cast<Events*>(user);
    std::lock_guard<std::mutex> lock(self.mutex);
    self.items.emplace_back(name, Json::parse(text));
    self.condition.notify_all();
  }

  Json Wait(const std::string& name, uint64_t id = 0,
            std::chrono::seconds timeout = 5s) {
    std::unique_lock<std::mutex> lock(mutex);
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    for (;;) {
      for (auto it = items.begin(); it != items.end(); ++it) {
        if (it->first == name &&
            (!id || it->second.value("id", uint64_t(0)) == id)) {
          Json result = it->second;
          items.erase(it);
          return result;
        }
      }
      if (condition.wait_until(lock, deadline) ==
          std::cv_status::timeout) {
        throw std::runtime_error("等待客户端事件超时：" + name);
      }
    }
  }

  std::mutex mutex;
  std::condition_variable condition;
  std::deque<std::pair<std::string, Json>> items;
};

// 本地信令服务器与一个核心库实例，模拟服务端下发的完整会话。
struct Fixture {
  Fixture() {
    server.clear_access_channels(websocketpp::log::alevel::all);
    server.clear_error_channels(websocketpp::log::elevel::all);
    server.init_asio();
    server.set_validate_handler([this](auto handle) {
      auto connection = server.get_con_from_hdl(handle);
      const bool good =
          connection->get_request_header("Authorization") ==
              "Bearer test-only" &&
          connection->get_requested_subprotocols() ==
              std::vector<std::string>{"talk.v1.json"};
      if (good) {
        connection->select_subprotocol("talk.v1.json");
      }
      return good;
    });
    server.set_open_handler([this](auto handle) {
      connection = handle;
      ++opens;
    });
    server.set_message_handler(
        [this](auto handle, Server::message_ptr message) {
          OnMessage(handle, message);
        });
    server.listen(asio::ip::tcp::endpoint(
        asio::ip::make_address("127.0.0.1"), 0));
    asio::error_code error;
    const auto port = server.get_local_endpoint(error).port();
    Check(!error, "读取监听端口失败");
    url = "ws://127.0.0.1:" + std::to_string(port) + "/talk/v1/ws";
    server.start_accept();
    thread = std::thread([this] { server.run(); });
    talk_options options{sizeof(talk_options), TALK_ABI_VERSION,
                         Events::Callback, &events, "{}"};
    core = talk_create(&options);
    Check(core != nullptr, "创建失败");
  }

  ~Fixture() {
    talk_destroy(core);
    server.stop();
    thread.join();
  }

  void OnMessage(websocketpp::connection_hdl handle,
                 Server::message_ptr message) {
    const Json message_json = Json::parse(message->get_payload());
    if (message_json["type"] == "res") {
      Events::Callback(&replies, "server-result",
                       message->get_payload().c_str());
      return;
    }
    if (message_json["type"] != "req") {
      return;
    }
    const std::string method = message_json["method"].get<std::string>();
    Json data = Json::object();
    if (method == "session.hello") {
      const bool resumed = message_json["data"].contains("resume");
      data = {{"session_id", "test-session"},
              {"uid", "self"},
              {"resumed", resumed},
              {"heartbeat_interval", 30},
              {"resume_ttl", 60},
              {"ice_servers", Json::array()}};
      if (resumed && joined) {
        data["room"] = Snapshot();
      }
    } else if (method == "room.join") {
      ++joins;
      Check(message_json["data"]["media"]["audio"]["muted"] == true,
            "初始状态不是静音");
      joined = true;
      data = Snapshot();
    } else if (method == "room.leave") {
      joined = false;
    } else if (method == "media.setMute") {
      if (!message_json["data"]["muted"].get<bool>() && delay_unmute) {
        delayed = message_json;
        return;
      }
      data = {{"audio", {{"muted", message_json["data"]["muted"]}}}};
    } else if (method == "session.bye") {
      ++byes;
      joined = false;
    } else if (method == "session.ping" && drop_ping) {
      return;
    }
    Reply(handle,
          talk::Response(message_json["id"].get<std::string>(), data).dump());
  }

  void Reply(websocketpp::connection_hdl handle, const std::string& text) {
    server.send(handle, text, websocketpp::frame::opcode::text);
  }

  void Reply(const std::string& text) { Reply(connection, text); }

  uint64_t Command(const char* method, Json data = Json::object()) {
    const std::string text = data.dump();
    const uint64_t id = talk_command(core, method, text.c_str());
    Check(id != 0, "命令提交失败");
    return id;
  }

  Json Result(uint64_t id, std::chrono::seconds timeout = 5s) {
    return events.Wait("result", id, timeout);
  }

  void Connect() {
    const Json config = {{"url", url}, {"token", "test-only"}};
    Check(Result(Command("connect", config))["ok"] == true, "连接失败");
    events.Wait("ready");
  }

  // 交给服务器线程执行，便于访问连接句柄与内部计数。
  template <class F>
  void OnServer(F task) {
    std::promise<void> done;
    std::future<void> finished = done.get_future();
    server.get_io_service().post([task = std::move(task), &done] {
      try {
        task();
        done.set_value();
      } catch (...) {
        done.set_exception(std::current_exception());
      }
    });
    finished.get();
  }

  Json Snapshot() {
    return {{"room", {{"room_id", "test-room"}}}, {"peers", Json::array()}};
  }

  Server server;
  websocketpp::connection_hdl connection;
  std::thread thread;
  Events events;
  Events replies;
  talk_handle* core = nullptr;
  std::string url;
  bool joined = false;
  bool delay_unmute = false;
  bool drop_ping = false;
  int joins = 0;
  int byes = 0;
  int opens = 0;
  Json delayed;
};

}  // namespace

int main() {
  try {
    Fixture fixture;
    Check(!talk_command(nullptr, "x", "{}"), "空句柄必须拒绝");
    Check(!talk_command(fixture.core, "x", "[]"), "非对象参数必须拒绝");
    Check(fixture.Result(fixture.Command("room.join"))["ok"] == false,
          "连接前必须拒绝入房");
    fixture.Connect();
    Check(fixture.Result(fixture.Command("room.join",
                                         {{"room_id", "test-room"}}))["ok"] ==
              true,
          "入房失败");
    fixture.events.Wait("joined");
    fixture.OnServer([&] {
      const Json request =
          talk::MakeRequest("s-unknown", "future.method", Json::object());
      fixture.Reply(request.dump());
    });
    Check(fixture.replies.Wait("server-result")["error"]["code"] == 501,
          "未知服务器请求未回 501");

    fixture.OnServer([&] { fixture.delay_unmute = true; });
    const uint64_t unmute =
        fixture.Command("media.setMute", {{"muted", false}});
    const uint64_t mute = fixture.Command("media.setMute", {{"muted", true}});
    Check(fixture.Result(mute)["ok"] == true, "静音失败");
    fixture.OnServer([&] {
      const Json response =
          talk::Response(fixture.delayed["id"].get<std::string>(),
                         {{"audio", {{"muted", false}}}});
      fixture.Reply(response.dump());
    });
    Check(fixture.Result(unmute)["ok"] == false,
          "迟到开麦应答不能再作为成功报告");

    fixture.OnServer([&] {
      fixture.server.get_con_from_hdl(fixture.connection)->terminate(
          websocketpp::lib::error_code());
    });
    const Json ready = fixture.events.Wait("ready");
    Check(ready["resumed"] == true, "未尝试恢复原会话");
    fixture.events.Wait("joined");
    fixture.OnServer([&] {
      Check(fixture.joins == 1, "恢复快照后重复入房");
      fixture.drop_ping = true;
    });
    const uint64_t ping = fixture.Command("session.ping");
    Check(fixture.Result(ping, 12s)["error"]["code"] == 504,
          "请求未按时超时");
    Check(fixture.Result(fixture.Command("disconnect"))["ok"] == true,
          "断开失败");
    fixture.OnServer([&] { Check(fixture.byes == 1, "正常断开未发送 bye"); });

    fixture.Connect();
    fixture.OnServer([&] {
      fixture.Reply(
          talk::MakeNotification("session.kicked", {{"reason", "replaced"}})
              .dump());
    });
    fixture.events.Wait("session.kicked");
    std::this_thread::sleep_for(1500ms);
    fixture.OnServer([&] {
      Check(fixture.opens == 3, "顶号后不应自动重新连接");
    });
    std::cout << "信令：握手、入房、双向请求、迟到开麦、resume、超时、bye、"
                 "顶号通过\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
