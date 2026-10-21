// File: tests/protocol_test.cpp
// Brief: 校验信令报文的解析、构造与拒绝规则。
#include "signaling/protocol.h"

#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using talk::Json;

// 合法报文应能解析，且关键字段保持不变。
void CheckValidMessages() {
  const std::vector<std::string> messages = {
      R"({"v":1,"type":"req","id":"s-1","method":"sfu.offer",)"
      R"("extra":true})",
      R"({"v":1,"type":"res","id":"c-1","ok":true,"data":{}})",
      R"({"v":1,"type":"res","id":"c-1","ok":false,)"
      R"("error":{"code":403}})",
      R"({"v":1,"type":"ntf","method":"room.peerLeft",)"
      R"("data":{"uid":"甲"}})",
  };
  for (const auto& text : messages) {
    talk::ParseMessage(text);
  }
}

// 非法报文必须抛出，避免把坏数据带进状态机。
void CheckInvalidMessages() {
  const std::vector<std::string> messages = {
      "{",
      "[]",
      "null",
      "{}",
      R"({"v":2,"type":"req","id":"c-1","method":"x"})",
      R"({"v":1,"type":"req","id":1,"method":"x"})",
      R"({"v":1,"type":"res","id":"c-1","ok":"true"})",
      R"({"v":1,"type":"res","id":"c-1","ok":false})",
      R"({"v":1,"type":"req","id":"","method":"x"})",
      R"({"v":1,"type":"ntf","method":false})",
      R"({"v":1,"type":"bogus"})",
      std::string(talk::kMaxMessage + 1, 'x'),
  };
  for (const auto& text : messages) {
    bool threw = false;
    try {
      talk::ParseMessage(text);
    } catch (...) {
      threw = true;
    }
    if (!threw) {
      throw std::runtime_error("无效报文被接受");
    }
  }
}

// 构造出的响应应能被自身解析。
void CheckRoundTrip() {
  const Json response =
      talk::ParseMessage(talk::Response("s-1", {{"sdp", "answer"}}).dump());
  if (response["id"] != "s-1") {
    throw std::runtime_error("响应 id 不匹配");
  }
  const Json failure =
      talk::ParseMessage(talk::Failure("s-2", 501, "未实现").dump());
  if (failure["error"]["code"] != 501) {
    throw std::runtime_error("错误码不匹配");
  }
}

}  // namespace

int main() {
  try {
    CheckValidMessages();
    CheckInvalidMessages();
    CheckRoundTrip();
    std::cout << "协议：合法/非法报文与响应构造通过\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
