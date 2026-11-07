#include "rtc_client/protocol/message.h"

namespace rtc_client::protocol {
namespace {
void check_tree(const Json& value, unsigned depth = 0) {
  if (depth > 32) throw InvalidMessage();
  if (value.is_array() && value.size() > 1024) throw InvalidMessage();
  if (value.is_object() && value.size() > 128) throw InvalidMessage();
  if (value.is_string() && value.get_ref<const std::string&>().size() > max_message_bytes)
    throw InvalidMessage();
  if (value.is_structured()) {
    for (auto it = value.begin(); it != value.end(); ++it) {
      if (value.is_object() && it.key().size() > 128) throw InvalidMessage();
      check_tree(it.value(), depth + 1);
    }
  }
}
bool valid_id(const std::string& id) {
  if (id.empty() || id.size() > 128) return false;
  for (unsigned char c : id) if (c < 0x21 || c > 0x7e) return false;
  return true;
}
std::string encode(Json object) {
  try {
    check_tree(object);
    auto text = object.dump();
    if (text.size() > max_message_bytes) throw InvalidMessage();
    (void)decode(text);  // Apply exactly the same contract in both directions.
    return text;
  } catch (...) { throw InvalidMessage(); }
}
}  // namespace

bool valid_method(std::string_view method) noexcept {
  if (method.empty() || method.size() > 128) return false;
  for (unsigned char c : method) {
    if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
          (c >= '0' && c <= '9') || c == '.' || c == '_' || c == '-')) return false;
  }
  return true;
}

Message decode(std::string_view text) {
  if (text.empty() || text.size() > max_message_bytes) throw InvalidMessage();
  try {
    auto j = Json::parse(text.begin(), text.end(), [](int depth, Json::parse_event_t, Json&) {
      if (depth > 32) throw InvalidMessage();
      return true;
    });
    check_tree(j);
    if (!j.is_object() || !j.contains("v") || !j["v"].is_number_integer() || j["v"] != 1 ||
        !j.contains("type") || !j["type"].is_string()) throw InvalidMessage();
    Message out;
    const auto type = j["type"].get<std::string>();
    if (type == "req") out.kind = Kind::request;
    else if (type == "res") out.kind = Kind::response;
    else if (type == "ntf") out.kind = Kind::notification;
    else throw InvalidMessage();
    if (out.kind != Kind::notification) {
      out.id = j.at("id").get<std::string>();
      if (!valid_id(out.id)) throw InvalidMessage();
    }
    if (out.kind != Kind::response) {
      out.method = j.at("method").get<std::string>();
      if (!valid_method(out.method)) throw InvalidMessage();
    } else {
      out.ok = j.at("ok").get<bool>();
      if (!out.ok) {
        const auto& error = j.at("error");
        const auto& code = error.at("code");
        if (!error.is_object() || !code.is_number_integer() || code < 100 || code > 999)
          throw InvalidMessage();
        out.error_code = code.get<int>();
      }
    }
    if (j.contains("data")) {
      if (!j["data"].is_object()) throw InvalidMessage();
      out.data = std::move(j["data"]);
    }
    return out;
  } catch (...) { throw InvalidMessage(); }  // Never expose raw input in exception text.
}

std::string request(std::string id, std::string method, const Json& data) {
  return encode({{"v",1},{"type","req"},{"id",std::move(id)},
                 {"method",std::move(method)},{"data",data}});
}
std::string success(std::string id, const Json& data) {
  return encode({{"v",1},{"type","res"},{"id",std::move(id)},{"ok",true},{"data",data}});
}
std::string failure(std::string id, int code) {
  return encode({{"v",1},{"type","res"},{"id",std::move(id)},{"ok",false},
                 {"error",{{"code",code},{"msg","request failed"}}}});
}
std::string notification(std::string method, const Json& data) {
  return encode({{"v",1},{"type","ntf"},{"method",std::move(method)},{"data",data}});
}
}  // namespace rtc_client::protocol
