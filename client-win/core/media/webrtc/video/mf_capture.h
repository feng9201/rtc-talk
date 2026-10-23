// File: core/media/webrtc/video/mf_capture.h
// Brief: Media Foundation 摄像头采集（NV12 紧凑输出），供 CameraVideoSource 使用。
//
// 与 device_tool/camera_cap 的 MFAPICapture 同源，去掉 FFmpeg/OpenCV/ONNX
// 依赖，只保留 Source Reader 采集。
#ifndef TALK_MEDIA_WEBRTC_VIDEO_MF_CAPTURE_H_
#define TALK_MEDIA_WEBRTC_VIDEO_MF_CAPTURE_H_

#include <cstdint>
#include <list>
#include <memory>
#include <set>
#include <string>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>

#include "media/webrtc/video/com_pointer.h"

namespace talk {
namespace camera {

struct FrameSize {
  int width;
  int height;

  bool operator<(const FrameSize& other) const {
    if (width != other.width) return width < other.width;
    return height < other.height;
  }
};

struct CameraInfo {
  int index = -1;
  std::string name;
  std::list<FrameSize> supportedFrameSize;
  // 支持的格式 0：NV12 1：YUY2 2：MJPG 3：RGB24。
  std::list<int> supportedFormat;
  // 支持的帧率，如 {5, 10, 15, 20, 25, 30}。
  std::set<int> supportedFps;
};

// Media Foundation Source Reader 采集封装。
// 线程约束：init/doCapture 必须在同一个 COM 套间（MTA）线程调用，
// CameraVideoSource 用独立采集线程满足该约束。
class MFAPICapture {
 public:
  static std::list<CameraInfo> EnumerateDevices();

  bool init(const CameraInfo& camera_info, int width, int height, int fps = 25);

  std::string getCurErr();

  inline void setBuffer(std::shared_ptr<uint8_t> buffer) {
    m_buffer = std::move(buffer);
    m_buffer_size = 0;
  }

  // 带上缓冲区大小，doCapture 会先校验 NV12 一帧是否放得下。
  inline void setBuffer(std::shared_ptr<uint8_t> buffer, size_t size) {
    m_buffer = std::move(buffer);
    m_buffer_size = size;
  }

  bool doCapture();

  // 实际输出布局：始终是紧凑 NV12（行宽 = width）。
  int width() const { return m_width; }
  int height() const { return m_height; }
  int stride() const { return m_width; }

  // 累计成功帧数，以及相对首帧的时间戳（微秒），可用于丢帧/抖动统计。
  uint64_t frameIndex() const { return m_frame_index; }
  int64_t lastTimestampUs() const { return m_last_timestamp_us; }

  bool isInited() const { return m_pSourceReader.Get() != nullptr; }

  MFAPICapture() = default;
  ~MFAPICapture();

  MFAPICapture(const MFAPICapture&) = delete;
  MFAPICapture& operator=(const MFAPICapture&) = delete;

 private:
  bool createDevice(const CameraInfo& camera_info);
  bool setFrameInfo(int width, int height, int fps);
  // 媒体类型变化后重新读取行宽。
  void refreshStride();

  ComPointer<IMFSourceReader> m_pSourceReader;
  ComPointer<IMFMediaSource> m_pDevice;

  std::shared_ptr<uint8_t> m_buffer{nullptr};
  size_t m_buffer_size = 0;

  int m_width = 0;
  int m_height = 0;
  int m_fps = 0;
  int m_src_stride = 0;  // 设备实际行宽，可能大于 width（驱动对齐）

  uint64_t m_frame_index = 0;
  int64_t m_last_timestamp_us = 0;
  int64_t m_first_sample_time = 0;

  bool m_com_inited = false;
  bool m_mf_started = false;

  std::string m_strErr;
};

}  // namespace camera
}  // namespace talk

#endif  // TALK_MEDIA_WEBRTC_VIDEO_MF_CAPTURE_H_
