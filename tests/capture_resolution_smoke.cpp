// Manual, short hardware smoke: no network and no images saved.
#include "desktop_video_source.h"
#include "video/mf_h264_codec.h"
#include "api/make_ref_counted.h"
#include "rtc_base/logging.h"
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <vector>
#include <thread>

bool CaptureSize(bool native, int* width, int* height) {
  auto source = webrtc::make_ref_counted<rtcwin::webrtc_media::DesktopVideoSource>(1920, 1080, 15);
  if (!source->SetNativeResolution(native)) return false;
  std::mutex mutex;
  std::condition_variable ready;
  bool received = false;
  source->SetFrameCallback([&](const rtcwin::webrtc_media::VideoFrameData& frame) {
    std::lock_guard<std::mutex> lock(mutex);
    *width = frame.width;
    *height = frame.height;
    received = true;
    ready.notify_all();
  });
  std::string error;
  if (!source->Start(&error)) { std::cerr << error << '\n'; return false; }
  const bool locked = !source->SetNativeResolution(!native);
  bool ok;
  { std::unique_lock<std::mutex> lock(mutex);
    ok = ready.wait_for(lock, std::chrono::seconds(4), [&] { return received; }); }
  source->Stop();
  std::cout << "desktop native=" << native << " size=" << *width << 'x' << *height
            << " locked=" << locked << '\n';
  return ok && locked && source->SetNativeResolution(!native);
}

bool EncodeDecode(int width, int height) {
  rtcwin::webrtc_media::MfH264Encoder encoder;
  rtcwin::webrtc_media::MfH264Decoder decoder;
  if (!encoder.Open(width, height, 15, 4000000)) {
    std::cerr << "encoder open failed " << width << 'x' << height << '\n'; return false;
  }
  std::vector<uint8_t> nv12(static_cast<size_t>(width) * height * 3 / 2, 128);
  std::fill_n(nv12.begin(), static_cast<size_t>(width) * height, 96);
  for (int i = 0; i < 50; ++i) {
    std::string payload;
    bool key = false;
    if (!encoder.Encode(nv12.data(), width, (i + 1) * 66667LL, i == 0, &payload, &key)) return false;
    if (payload.empty()) { std::this_thread::sleep_for(std::chrono::milliseconds(30)); continue; }
    std::cout << "encoded bytes=" << payload.size() << " key=" << key << '\n';
    std::vector<uint8_t> decoded;
    int w = 0, h = 0;
    if (decoder.Decode(reinterpret_cast<const uint8_t*>(payload.data()), payload.size(),
                       (i + 1) * 66667LL, &decoded, &w, &h)) {
      std::cout << "H264 roundtrip=" << w << 'x' << h << '\n';
      return w == width && h == height;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
  }
  std::cerr << "no decoded frame " << width << 'x' << height << '\n';
  return false;
}

int main() {
  webrtc::LogMessage::SetLogToStderr(true);
  webrtc::LogMessage::LogToDebug(webrtc::LS_INFO);
  int native_width = 0, native_height = 0, width = 0, height = 0;
  if (!CaptureSize(true, &native_width, &native_height) ||
      !CaptureSize(false, &width, &height)) return 1;
  const double scale = std::min({1.0, 1920.0 / native_width, 1080.0 / native_height});
  if (width != (static_cast<int>(native_width * scale) & ~1) ||
      height != (static_cast<int>(native_height * scale) & ~1)) return 2;
  if (!EncodeDecode(1280, 720) || !EncodeDecode(1920, 1080) ||
      !EncodeDecode(native_width, native_height)) return 3;
  return 0;
}
