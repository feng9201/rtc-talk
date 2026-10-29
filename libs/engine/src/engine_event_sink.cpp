#include "rtcwin/engine/engine_event_sink.h"

#include "engine_log.h"

namespace rtcwin::engine {

using signaling::Json;

EngineEventSink::EngineEventSink(const rtcwin_options& options)
    : options_(options) {}

void EngineEventSink::EmitResponse(std::uint64_t id,
                                   const std::string& method, bool ok,
                                   int error_code,
                                   const std::string& message,
                                   const Json& data) {
  LOG_INFO("response id={} method={} ok={} code={} message={}", id, method, ok,
           error_code, message);
  if (options_.on_event == nullptr) return;
  Json envelope = {{"v", 1},
                   {"type", "res"},
                   {"id", id},
                   {"method", method},
                   {"ok", ok}};
  if (ok) {
    envelope["data"] = data.is_null() ? Json::object() : data;
  } else {
    envelope["error"] = {{"code", error_code}, {"msg", message}};
  }
  const std::string text = envelope.dump();
  options_.on_event(options_.userdata, method.c_str(), text.c_str());
}

void EngineEventSink::EmitNotification(const std::string& method,
                                       const Json& data) {
  LOG_INFO("notification method={}", method);
  if (options_.on_event == nullptr) return;
  Json envelope = {
      {"v", 1}, {"type", "ntf"}, {"method", method},
      {"data", data.is_null() ? Json::object() : data}};
  const std::string text = envelope.dump();
  options_.on_event(options_.userdata, method.c_str(), text.c_str());
}

}  // namespace rtcwin::engine
