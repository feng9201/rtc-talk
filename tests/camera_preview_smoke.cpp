#include "rtcwin/client_api.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

struct SmokeContext {
  std::mutex mutex;
  std::condition_variable cv;
  std::vector<std::string> events;
  std::atomic<uint64_t> frames{0};
};

int IntArg(int argc, char** argv, const std::string& name, int fallback) {
  for (int i = 1; i + 1 < argc; ++i) {
    if (argv[i] == name) {
      try {
        return std::stoi(argv[i + 1]);
      } catch (...) {
        return fallback;
      }
    }
  }
  return fallback;
}

void OnEvent(void* userdata, const char*, const char* json) {
  auto* context = static_cast<SmokeContext*>(userdata);
  if (context == nullptr || json == nullptr) return;
  {
    std::lock_guard<std::mutex> lock(context->mutex);
    context->events.emplace_back(json);
  }
  context->cv.notify_all();
}

void OnLocalFrame(void* userdata, const rtcwin_frame_view* frame) {
  auto* context = static_cast<SmokeContext*>(userdata);
  if (context == nullptr || frame == nullptr || frame->i420_y == nullptr ||
      frame->width <= 0 || frame->height <= 0) {
    return;
  }
  context->frames.fetch_add(1);
  context->cv.notify_all();
}

bool WaitForFrames(SmokeContext* context, uint64_t target,
                   std::chrono::milliseconds timeout) {
  std::unique_lock<std::mutex> lock(context->mutex);
  return context->cv.wait_for(lock, timeout, [context, target] {
    return context->frames.load() >= target;
  });
}

bool WaitForResponse(SmokeContext* context, uint64_t id,
                     const std::string& method,
                     std::chrono::milliseconds timeout, bool* ok,
                     std::string* event_json) {
  const std::string id_text = "\"id\":" + std::to_string(id);
  const std::string method_text = "\"method\":\"" + method + "\"";
  std::unique_lock<std::mutex> lock(context->mutex);
  const bool found = context->cv.wait_for(lock, timeout, [&] {
    for (const std::string& event : context->events) {
      if (event.find("\"type\":\"res\"") != std::string::npos &&
          event.find(id_text) != std::string::npos &&
          event.find(method_text) != std::string::npos) {
        return true;
      }
    }
    return false;
  });
  if (!found) return false;
  for (const std::string& event : context->events) {
    if (event.find("\"type\":\"res\"") != std::string::npos &&
        event.find(id_text) != std::string::npos &&
        event.find(method_text) != std::string::npos) {
      if (ok != nullptr) *ok = event.find("\"ok\":true") != std::string::npos;
      if (event_json != nullptr) *event_json = event;
      return true;
    }
  }
  return false;
}

bool SubmitAndWait(rtcwin_handle* handle, SmokeContext* context,
                   const std::string& method, const std::string& payload,
                   std::chrono::milliseconds timeout) {
  const uint64_t id =
      rtcwin_submit(handle, method.c_str(), payload.c_str(), 15000);
  if (id == 0) {
    std::cerr << "submit failed: " << method << '\n';
    return false;
  }
  bool ok = false;
  std::string event;
  if (!WaitForResponse(context, id, method, timeout, &ok, &event)) {
    std::cerr << "response timeout: " << method << '\n';
    return false;
  }
  if (!ok) {
    std::cerr << "command failed: " << method << " " << event << '\n';
    return false;
  }
  return true;
}

}  // namespace

int main(int argc, char** argv) {
  int cycles = IntArg(argc, argv, "--cycles", 3);
  int hold_ms = IntArg(argc, argv, "--hold-ms", 500);
  const int device_index = IntArg(argc, argv, "--device-index", -1);
  if (cycles < 1 || cycles > 1000 || hold_ms < 0 || hold_ms > 60000) {
    std::cerr << "invalid arguments\n";
    return 2;
  }

  SmokeContext context;
  rtcwin_options options{};
  options.struct_size = sizeof(options);
  options.abi_version = RTCWIN_ABI_VERSION;
  options.userdata = &context;
  options.on_event = &OnEvent;
  options.on_local_frame = &OnLocalFrame;
  rtcwin_handle* handle = rtcwin_create(&options);
  if (handle == nullptr) {
    std::cerr << "engine create failed\n";
    return 3;
  }

  const bool devices_ok =
      SubmitAndWait(handle, &context, "camera.devices", "{}",
                    std::chrono::seconds(15));
  if (!devices_ok) {
    rtcwin_destroy(handle);
    return 4;
  }

  for (int cycle = 1; cycle <= cycles; ++cycle) {
    const uint64_t first_frame = context.frames.load() + 1;
    const std::string payload =
        "{\"device_index\":" + std::to_string(device_index) + "}";
    if (!SubmitAndWait(handle, &context, "camera.preview.start", payload,
                       std::chrono::seconds(15))) {
      rtcwin_destroy(handle);
      return 5;
    }
    if (!WaitForFrames(&context, first_frame, std::chrono::seconds(10))) {
      std::cerr << "no camera frame in cycle " << cycle << '\n';
      rtcwin_destroy(handle);
      return 6;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(hold_ms));
    if (!SubmitAndWait(handle, &context, "camera.preview.stop", "{}",
                       std::chrono::seconds(10))) {
      rtcwin_destroy(handle);
      return 7;
    }
    const uint64_t stopped_at = context.frames.load();
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    if (context.frames.load() != stopped_at) {
      std::cerr << "frame arrived after stop in cycle " << cycle << '\n';
      rtcwin_destroy(handle);
      return 8;
    }
    std::cout << "cycle " << cycle << '/' << cycles
              << " frames=" << context.frames.load() << '\n';
  }

  rtcwin_destroy(handle);
  std::cout << "camera preview smoke passed: cycles=" << cycles
            << " frames=" << context.frames.load() << '\n';
  return 0;
}