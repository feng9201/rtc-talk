#include "test.h"
#include "rtc_client/protocol/message.h"
#include <vector>
using namespace rtc_client::protocol;
int main() {
  return test_main([] {
    auto req = decode(request("c-1","room.join",{{"room_id","rk-client"}}));
    CHECK(req.kind == Kind::request && req.id == "c-1" && req.method == "room.join");
    CHECK(req.data["room_id"] == "rk-client");
    auto res = decode(success("s-1",{{"sdp","v=0"}}));
    CHECK(res.kind == Kind::response && res.ok && res.data["sdp"] == "v=0");
    CHECK(decode(failure("s-2",501)).error_code == 501);
    CHECK(decode(notification("session.kicked",{{"reason","replaced"}})).kind == Kind::notification);
    CHECK(decode(R"({"v":1,"type":"ntf","method":"room.peerLeft","future":true})").data.empty());
    const std::vector<std::string> bad = {
      "", "{", "[]", "null", "true", R"({"v":1.0,"type":"req"})",
      R"({"v":2,"type":"ntf","method":"room.closed"})",
      R"({"v":1,"type":"unknown"})", R"({"v":1,"type":"req","method":"x"})",
      R"({"v":1,"type":"req","id":"","method":"x"})",
      R"({"v":1,"type":"req","id":1,"method":"x"})",
      R"({"v":1,"type":"req","id":"c\n1","method":"x"})",
      R"({"v":1,"type":"ntf","method":"a b"})",
      R"({"v":1,"type":"ntf","method":"room.x","data":[]})",
      R"({"v":1,"type":"res","id":"c-1","ok":1})",
      R"({"v":1,"type":"res","id":"c-1","ok":false})",
      R"({"v":1,"type":"res","id":"c-1","ok":false,"error":{"code":"403"}})",
      R"({"v":1,"type":"res","id":"c-1","ok":false,"error":{"code":9999999999999999999}})",
      std::string(max_message_bytes + 1,'x'),
      std::string(40,'[') + "0" + std::string(40,']')
    };
    for (const auto& text : bad) {
      bool rejected = false;
      try { (void)decode(text); }
      catch (const InvalidMessage& e) { rejected = true; CHECK(std::string(e.what()) == "Invalid talk.v1 envelope"); }
      CHECK(rejected);
    }
    bool rejected = false;
    try { (void)request("c-1","bad method",Json::object()); } catch (const InvalidMessage&) { rejected = true; }
    CHECK(rejected);
    rejected = false;
    try { (void)success("c-1",{{"huge",std::string(max_message_bytes,'x')}}); }
    catch (const InvalidMessage&) { rejected = true; }
    CHECK(rejected);
  });
}
