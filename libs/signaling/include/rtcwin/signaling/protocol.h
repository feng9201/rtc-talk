// File: core/signaling/protocol.h
// Brief: talk.v1.json 协议信封的校验、构造与字段约定。
#ifndef TALK_SIGNALING_PROTOCOL_H_
#define TALK_SIGNALING_PROTOCOL_H_

#include <cstddef>
#include <stdexcept>
#include <string>

#include "rtcwin/signaling/json.h"

namespace rtcwin::signaling {

// 单条信令报文上限，发送与接收两侧共用。
inline constexpr size_t kMaxMessage = 256 * 1024;

// 校验信封并返回报文；无效时抛 std::runtime_error。
inline Json ParseMessage(const std::string& text) {
  if (text.size() > kMaxMessage) {
    throw std::runtime_error("消息过大");
  }
  Json message = Json::parse(text);
  if (!message.is_object() || !message.contains("v") ||
      !message["v"].is_number_integer() || message["v"] != 1 ||
      !message.contains("type") || !message["type"].is_string()) {
    throw std::runtime_error("协议封装无效");
  }
  const std::string type = message["type"].get<std::string>();
  if (type != "req" && type != "res" && type != "ntf") {
    throw std::runtime_error("消息类型无效");
  }
  if (type != "ntf" &&
      (!message.contains("id") || !message["id"].is_string() ||
       message["id"].get<std::string>().empty())) {
    throw std::runtime_error("缺少请求 ID");
  }
  if (type != "res" &&
      (!message.contains("method") || !message["method"].is_string() ||
       message["method"].get<std::string>().empty())) {
    throw std::runtime_error("缺少 method");
  }
  if (type == "res" &&
      (!message.contains("ok") || !message["ok"].is_boolean())) {
    throw std::runtime_error("缺少响应状态");
  }
  if (type == "res" && !message["ok"].get<bool>() &&
      (!message.contains("error") || !message["error"].is_object() ||
       !message["error"].contains("code") ||
       !message["error"]["code"].is_number_integer())) {
    throw std::runtime_error("错误响应无效");
  }
  return message;
}

// 成功响应。
inline Json Response(const std::string& id, const Json& data) {
  return {{"v", 1}, {"type", "res"}, {"id", id}, {"ok", true}, {"data", data}};
}

// 失败响应；code 使用 talk 协议错误码。
inline Json Failure(const std::string& id, int code,
                    const std::string& message) {
  return {{"v", 1},
          {"type", "res"},
          {"id", id},
          {"ok", false},
          {"error", {{"code", code}, {"msg", message}}}};
}

// 客户端请求。
inline Json MakeRequest(const std::string& id, const std::string& method,
                        const Json& data) {
  return {{"v", 1},
          {"type", "req"},
          {"id", id},
          {"method", method},
          {"data", data}};
}

// 客户端通知；不等待响应。
inline Json MakeNotification(const std::string& method, const Json& data) {
  return {{"v", 1}, {"type", "ntf"}, {"method", method}, {"data", data}};
}

}  // namespace rtcwin::signaling

#endif  // TALK_SIGNALING_PROTOCOL_H_
