// File: core/c_api.cpp
// Brief: talk.h 的 C ABI 实现，只做参数校验与转发。
#include <exception>
#include <memory>
#include <string>
#include <utility>

#include "app/talk_client.h"
#include "base/json.h"
#include "base/log.h"
#include "signaling/protocol.h"
#include "talk/talk.h"

struct talk_handle {
  std::unique_ptr<talk::TalkClient> client;
};

extern "C" {

const char* talk_version(void) { return "0.1.0"; }

talk_handle* talk_create(const talk_options* options) {
  if (options == nullptr || options->struct_size < sizeof(talk_options) ||
      options->abi_version != TALK_ABI_VERSION) {
    TALK_LOG_ERROR("capi", "创建失败：options 或 ABI 版本无效");
    return nullptr;
  }
  try {
    const char* text =
        options->config_json != nullptr ? options->config_json : "{}";
    talk::Json config = talk::Json::parse(text);
    if (!config.is_object()) {
      TALK_LOG_ERROR("capi", "创建失败：配置不是 JSON 对象");
      return nullptr;
    }
    const std::string backend =
        config.value("audio_backend", std::string("none"));
    if (backend != "none" && backend != "fake" && backend != "wasapi" &&
        backend != "default") {
      TALK_LOG_ERROR("capi", "创建失败：audio_backend={} 不支持",
                     backend);
      return nullptr;
    }
    auto handle = std::make_unique<talk_handle>();
    handle->client = std::make_unique<talk::TalkClient>(
        options->on_event, options->userdata, std::move(config));
    TALK_LOG_INFO("capi", "客户端已创建 version={}", talk_version());
    return handle.release();
  } catch (const std::exception& error) {
    TALK_LOG_ERROR("capi", "创建失败 error={}", error.what());
    return nullptr;
  } catch (...) {
    TALK_LOG_ERROR("capi", "创建失败：未知异常");
    return nullptr;
  }
}

uint64_t talk_command(talk_handle* handle, const char* method,
                      const char* text) {
  if (handle == nullptr || method == nullptr || *method == '\0' ||
      text == nullptr) {
    TALK_LOG_WARN("capi", "命令被拒绝：句柄或参数为空");
    return 0;
  }
  try {
    std::string input(text);
    if (input.size() > talk::kMaxMessage) {
      TALK_LOG_WARN("capi", "命令被拒绝：method={} size={} 超过上限",
                    method, input.size());
      return 0;
    }
    talk::Json data = talk::Json::parse(input);
    if (!data.is_object()) {
      TALK_LOG_WARN("capi", "命令被拒绝：method={} 数据不是对象",
                    method);
      return 0;
    }
    const uint64_t id = handle->client->Submit(method, std::move(data));
    if (id == 0) {
      TALK_LOG_WARN("capi", "命令被丢弃：method={} 队列已满", method);
    }
    return id;
  } catch (const std::exception& error) {
    TALK_LOG_ERROR("capi", "命令解析失败 method={} error={}", method,
                   error.what());
    return 0;
  } catch (...) {
    TALK_LOG_ERROR("capi", "命令处理失败 method={}：未知异常", method);
    return 0;
  }
}

void talk_set_video_frame_callback(talk_handle* handle,
                                  talk_video_frame_fn callback,
                                  void* userdata) {
  if (handle == nullptr || handle->client == nullptr) {
    TALK_LOG_WARN("capi", "注册视频帧回调被拒绝：句柄为空");
    return;
  }
  handle->client->SetVideoFrameCallback(callback, userdata);
}

void talk_destroy(talk_handle* handle) {
  TALK_LOG_DEBUG("capi", "销毁客户端");
  delete handle;
}

}  // extern "C"
