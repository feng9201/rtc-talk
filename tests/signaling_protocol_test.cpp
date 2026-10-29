#include "rtcwin/signaling/protocol.h"

#include <iostream>
#include <stdexcept>
#include <string>

namespace {

using rtcwin::signaling::Failure;
using rtcwin::signaling::Json;
using rtcwin::signaling::MakeNotification;
using rtcwin::signaling::MakeRequest;
using rtcwin::signaling::ParseMessage;
using rtcwin::signaling::Response;

void Check(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

bool Rejects(const std::string& text) {
  try {
    ParseMessage(text);
    return false;
  } catch (const std::exception&) {
    return true;
  }
}

}  // namespace

int main() {
  try {
    const Json request = MakeRequest("c-1", "session.hello", Json::object());
    Check(ParseMessage(request.dump())["method"] == "session.hello",
          "request round trip");

    const Json response = Response("c-1", {{"session_id", "s-1"}});
    Check(ParseMessage(response.dump())["data"]["session_id"] == "s-1",
          "response round trip");

    const Json notification = MakeNotification("room.peerJoined", Json::object());
    Check(ParseMessage(notification.dump())["type"] == "ntf",
          "notification round trip");

    Check(Rejects("{}"), "empty object must fail");
    Check(Rejects(R"({"v":2,"type":"req","id":"c-1","method":"x"})"),
          "version must fail");
    Check(Rejects(R"({"v":1,"type":"res","id":"c-1","ok":false})"),
          "failure response needs error");
    Check(!Rejects(Failure("c-1", 403, "denied").dump()),
          "failure response must parse");

    std::cout << "signaling protocol tests passed\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "signaling protocol test failed: " << error.what() << '\n';
    return 1;
  }
}