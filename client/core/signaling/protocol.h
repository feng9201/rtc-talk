#pragma once
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
namespace talk {
using Json = nlohmann::json;
constexpr size_t kMaxMessage = 256 * 1024;
inline Json parse_message(const std::string& text) {
  if (text.size() > kMaxMessage) throw std::runtime_error("消息过大");
  auto j = Json::parse(text);
  if (!j.is_object() || !j.contains("v") || !j["v"].is_number_integer() || j["v"] != 1 ||
      !j.contains("type") || !j["type"].is_string()) throw std::runtime_error("协议封装无效");
  const auto type = j["type"].get<std::string>();
  if (type != "req" && type != "res" && type != "ntf") throw std::runtime_error("消息类型无效");
  if (type != "ntf" && (!j.contains("id") || !j["id"].is_string() || j["id"].get<std::string>().empty()))
    throw std::runtime_error("缺少请求 ID");
  if (type != "res" && (!j.contains("method") || !j["method"].is_string() || j["method"].get<std::string>().empty()))
    throw std::runtime_error("缺少 method");
  if (type == "res" && (!j.contains("ok") || !j["ok"].is_boolean())) throw std::runtime_error("缺少响应状态");
  if (type == "res" && !j["ok"].get<bool>() &&
      (!j.contains("error") || !j["error"].is_object() || !j["error"].contains("code") || !j["error"]["code"].is_number_integer()))
    throw std::runtime_error("错误响应无效");
  return j;
}
inline Json response(const std::string& id, const Json& data) {
  return {{"v",1},{"type","res"},{"id",id},{"ok",true},{"data",data}};
}
inline Json failure(const std::string& id, int code, const std::string& msg) {
  return {{"v",1},{"type","res"},{"id",id},{"ok",false},{"error",{{"code",code},{"msg",msg}}}};
}
}
