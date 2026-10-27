#include "rtcwin/engine/engine_core.h"

#include <sstream>
#include <utility>

namespace rtcwin::engine {
namespace {

std::string EscapeJson(const std::string& value) {
  std::string result;
  result.reserve(value.size() + 8);
  for (char ch : value) {
    switch (ch) {
      case '"':
        result += "\\\"";
        break;
      case '\\':
        result += "\\\\";
        break;
      case '\n':
        result += "\\n";
        break;
      case '\r':
        result += "\\r";
        break;
      case '\t':
        result += "\\t";
        break;
      default:
        result += ch;
        break;
    }
  }
  return result;
}

}  // namespace

EngineCore::EngineCore(const rtcwin_options& options, std::string config_json)
    : options_(options), config_json_(std::move(config_json)) {}

EngineCore::~EngineCore() = default;

uint64_t EngineCore::Submit(std::string method, std::string json) {
  const uint64_t id = next_id_.fetch_add(1);
  if (method == "engine.ping") {
    EmitResponse(id, method, true, 0, {});
  } else {
    EmitResponse(id, method, false, 501,
                 method.rfind("session.", 0) == 0
                     ? "媒体引擎后端尚未接入"
                     : "功能尚未实现");
  }
  return id;
}

void EngineCore::EmitResponse(uint64_t id, const std::string& method, bool ok,
                              int error_code, const std::string& message) {
  if (options_.on_event == nullptr) return;
  std::ostringstream json;
  json << "{\"v\":1,\"type\":\"res\",\"id\":" << id
       << ",\"method\":\"" << EscapeJson(method) << "\",\"ok\":"
       << (ok ? "true" : "false");
  if (ok) {
    json << ",\"data\":{}";
  } else {
    json << ",\"error\":{\"code\":" << error_code << ",\"msg\":\""
         << EscapeJson(message) << "\"}";
  }
  json << "}";
  const std::string text = json.str();
  options_.on_event(options_.userdata, method.c_str(), text.c_str());
}

}  // namespace rtcwin::engine
