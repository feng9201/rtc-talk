#include "test.h"
#include "rtc_client/signaling/rpc.h"
#include <optional>
#include <vector>
using namespace rtc_client;
using namespace rtc_client::protocol;
using namespace std::chrono_literals;
int main() {
  return test_main([] {
    const RpcPeer::Time now{};
    std::vector<Message> wire;
    bool can_send = true;
    RpcPeer rpc([&](const std::string& text) { wire.push_back(decode(text)); return can_send; },2);
    int calls = 0, code = 0, handled = 0, notifications = 0;
    bool bad_callback = false;
    std::optional<ServerRequest> incoming;
    rpc.set_handlers([&](const ServerRequest& r) { ++handled; incoming = r; },
                     [&](const Message&) { ++notifications; });
    auto id = rpc.begin("session.ping",Json::object(),[&](RpcResult r) { ++calls; bad_callback |= !r.ok; },now);
    CHECK(!id.empty() && rpc.pending() == 1);
    // The server's ID intentionally equals a client ID: namespaces are separate.
    rpc.receive(decode(request(id,"sfu.offer",{{"sdp","v=0"}})),now);
    CHECK(incoming && handled == 1 && rpc.pending() == 1);
    rpc.receive(decode(request(id,"sfu.offer",{{"sdp","v=0"}})),now);
    CHECK(handled == 1);
    rpc.receive(decode(success(id,Json::object())),now);
    CHECK(calls == 1 && rpc.pending() == 0);
    rpc.receive(decode(success(id,Json::object())),now);
    CHECK(calls == 1);
    CHECK(rpc.respond(*incoming,{true,0,{{"sdp","answer"}}},now));
    CHECK(!rpc.respond(*incoming,{true,0,Json::object()},now));
    rpc.receive(decode(request(id,"sfu.offer",{{"sdp","v=0"}})),now);
    CHECK(handled == 1 && wire.back().ok && wire.back().data["sdp"] == "answer");
    rpc.receive(decode(notification("room.peerLeft",{{"uid","other"}})),now);
    CHECK(notifications == 1);
    auto one = rpc.begin("room.join",Json::object(),[&](RpcResult r) { ++calls; code = r.code; },now);
    auto two = rpc.begin("sfu.publish",Json::object(),[&](RpcResult r) { ++calls; code = r.code; },now,15s);
    CHECK(!one.empty() && !two.empty());
    CHECK(rpc.begin("session.ping",Json::object(),[](RpcResult) {},now).empty());
    rpc.tick(now + 10s);
    CHECK(calls == 2 && code == 504 && rpc.pending() == 1);
    rpc.receive(decode(success(two,Json::object())),now + 15s);
    CHECK(calls == 3 && code == 504);
    can_send = false;
    CHECK(!rpc.begin("session.ping",Json::object(),[&](RpcResult r) { ++calls; bad_callback |= r.code != 480; },now).empty());
    CHECK(calls == 4 && rpc.pending() == 0);
    can_send = true;
    auto pending = rpc.begin("room.join",Json::object(),[&](RpcResult r) { ++calls; bad_callback |= r.code != 487; },now);
    CHECK(!pending.empty());
    rpc.receive(decode(request("s-2","sfu.offer",Json::object())),now);
    const auto stale = *incoming;
    rpc.reset(2);
    CHECK(calls == 5 && rpc.pending() == 0);
    CHECK(!rpc.respond(stale,{true,0,Json::object()},now));
    rpc.receive(decode(success(pending,Json::object())),now);
    CHECK(calls == 5);

    rpc.set_handlers({},{});
    rpc.receive(decode(request("s-3","future.method",Json::object())),now);
    CHECK(wire.back().error_code == 501);
    rpc.set_handlers([&](const ServerRequest& r) { incoming = r; },{});
    rpc.receive(decode(request("s-4","sfu.offer",Json::object())),now);
    CHECK(rpc.respond(*incoming,{true,0,Json::object()},now + 15s));
    CHECK(wire.back().error_code == 504);
    rpc.receive(decode(request("s-5","sfu.offer",Json::object())),now);
    rpc.tick(now + 15s);
    CHECK(wire.back().id == "s-5" && wire.back().error_code == 504);
    CHECK(!rpc.respond(*incoming,{true,0,Json::object()},now));

    // A close invoked by a cancellation callback must win over outer reset.
    CHECK(!rpc.begin("session.ping",Json::object(),[&](RpcResult) { rpc.close(); },now).empty());
    rpc.reset(3);
    CHECK(rpc.begin("session.ping",Json::object(),[](RpcResult) {},now).empty());
    rpc.reset(4);
    CHECK(!rpc.begin("session.ping",Json::object(),[](RpcResult) { throw std::runtime_error("test"); },now).empty());
    rpc.close();  // callback exceptions never escape.
    CHECK(rpc.pending() == 0);
    CHECK(!bad_callback);
  });
}
