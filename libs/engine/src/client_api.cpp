#include "rtcwin/client_api.h"

#include "rtcwin/engine/engine_core.h"

#include <exception>
#include <memory>
#include <string>

struct rtcwin_handle {
  std::unique_ptr<rtcwin::engine::EngineCore> engine;
};

extern "C" {

const char* rtcwin_version(void) { return "0.1.0"; }

rtcwin_handle* rtcwin_create(const rtcwin_options* options) {
  if (options == nullptr || options->struct_size < sizeof(rtcwin_options) ||
      options->abi_version != RTCWIN_ABI_VERSION) {
    return nullptr;
  }
  try {
    auto handle = std::make_unique<rtcwin_handle>();
    handle->engine = std::make_unique<rtcwin::engine::EngineCore>(
        *options, std::string{});
    return handle.release();
  } catch (...) {
    return nullptr;
  }
}

void rtcwin_destroy(rtcwin_handle* handle) { delete handle; }

uint64_t rtcwin_submit(rtcwin_handle* handle, const char* method,
                       const char* json, int32_t timeout_ms) {
  (void)timeout_ms;
  if (handle == nullptr || handle->engine == nullptr || method == nullptr ||
      *method == '\0' || json == nullptr) {
    return 0;
  }
  try {
    return handle->engine->Submit(method, json);
  } catch (...) {
    return 0;
  }
}

}  // extern "C"
