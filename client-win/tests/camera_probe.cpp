// File: tests/camera_probe.cpp
// Brief: 摄像头采集冒烟测试：枚举设备、打开、计数送帧与耗时。
//
// 只验证 MF 采集 + NV12→I420 + AdaptedVideoTrackSource 送帧，
// 不依赖服务端，也不经过 H.264 编码。
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <string>
#include <thread>

#include "api/make_ref_counted.h"
#include "api/scoped_refptr.h"
#include "media/webrtc/video/camera_video_source.h"

int main() {
  auto devices = talk::camera::MFAPICapture::EnumerateDevices();
  std::printf("devices=%zu\n", devices.size());
  for (const auto& device : devices) {
    std::printf("  [%d] %s sizes=%zu fps=%zu\n", device.index,
                device.name.c_str(), device.supportedFrameSize.size(),
                device.supportedFps.size());
    for (const auto& size : device.supportedFrameSize) {
      std::printf("      size %dx%d\n", size.width, size.height);
    }
    for (int fps : device.supportedFps) {
      std::printf("      fps %d\n", fps);
    }
  }
  if (devices.empty()) {
    std::printf("no camera found\n");
    return 1;
  }

  // AdaptedVideoTrackSource 继承 RefCountInterface，只能由引用计数工厂创建。
  webrtc::scoped_refptr<talk::CameraVideoSource> source =
      webrtc::make_ref_counted<talk::CameraVideoSource>(1280, 720, 15);

  // 跑两轮：第二轮验证关掉后再开（对应 UI 反复点“开/关摄像头”）。
  uint64_t total_frames = 0;
  for (int round = 1; round <= 2; ++round) {
    const uint64_t before = source->frames();
    std::string error;
    if (!source->Start(&error)) {
      std::printf("round%d start failed: %s\n", round, error.c_str());
      return 2;
    }
    std::printf("round%d started device=%s size=%dx%d fps=%d\n", round,
                source->device_name().c_str(), source->video_width(),
                source->video_height(), source->video_fps());

    const int duration_ms = round == 1 ? 3000 : 1500;
    const auto begin = std::chrono::steady_clock::now();
    std::this_thread::sleep_for(std::chrono::milliseconds(duration_ms));
    const auto elapsed_ms =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - begin)
            .count();
    const uint64_t frames = source->frames() - before;
    source->Stop();
    total_frames += frames;

    const double fps = elapsed_ms > 0 ? frames * 1000.0 / elapsed_ms : 0.0;
    std::printf("round%d frames=%llu elapsed_ms=%lld fps=%.1f error=%s\n",
                round, static_cast<unsigned long long>(frames),
                static_cast<long long>(elapsed_ms), fps,
                source->last_error().c_str());
  }
  return total_frames > 0 ? 0 : 3;
}
