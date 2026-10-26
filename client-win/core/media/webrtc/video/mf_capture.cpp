// File: core/media/webrtc/video/mf_capture.cpp
// Brief: Media Foundation Source Reader 采集实现（NV12 紧凑输出）。
#include "media/webrtc/video/mf_capture.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include <comdef.h>  // _com_error
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>

#include "rtc_base/logging.h"

#pragma comment(lib, "mf.lib")
#pragma comment(lib, "mfplat.lib")
#pragma comment(lib, "mfuuid.lib")
#pragma comment(lib, "mfreadwrite.lib")

namespace talk {
namespace camera {
namespace {

// COM / Media Foundation 的成对初始化。
// 线程可能已经被其它组件用 STA 初始化过 COM，此时 CoInitializeEx 返回
// RPC_E_CHANGED_MODE：COM 可以直接用，但绝不能再调用 CoUninitialize，
// 否则会把别人的引用计数减掉。
class ComMfScope {
 public:
  ComMfScope() {
    const HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (hr == S_OK || hr == S_FALSE) {
      com_owned_ = true;
      com_ready_ = true;
    } else if (hr == RPC_E_CHANGED_MODE) {
      com_ready_ = true;
    }
    if (com_ready_ && SUCCEEDED(MFStartup(MF_VERSION))) {
      mf_owned_ = true;
    }
  }

  ~ComMfScope() {
    if (mf_owned_) {
      MFShutdown();
    }
    if (com_owned_) {
      CoUninitialize();
    }
  }

  bool ok() const { return com_ready_ && mf_owned_; }

 private:
  bool com_owned_ = false;
  bool com_ready_ = false;
  bool mf_owned_ = false;
};

std::string WCharToUtf8(const wchar_t* text) {
  if (!text) {
    return {};
  }
  const int len =
      WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
  if (len <= 0) {
    return {};
  }
  std::string out(static_cast<size_t>(len - 1), '\0');
  WideCharToMultiByte(CP_UTF8, 0, text, -1, out.data(), len, nullptr, nullptr);
  return out;
}

std::string HrToString(HRESULT hr) {
  char buf[16] = {};
  sprintf_s(buf, sizeof(buf), "0x%08X", static_cast<unsigned int>(hr));
  return buf;
}

void ReleaseDeviceArray(IMFActivate** devices, UINT32 count) {
  if (!devices) {
    return;
  }
  for (UINT32 i = 0; i < count; ++i) {
    devices[i]->Release();
  }
  CoTaskMemFree(devices);
}

std::wstring TCHARToWString(const TCHAR* tchar_str) {
  if (!tchar_str) {
    return L"";
  }
#ifdef UNICODE
  return std::wstring(tchar_str);
#else
  const int wide_len =
      MultiByteToWideChar(CP_ACP, 0, tchar_str, -1, nullptr, 0);
  if (wide_len == 0) {
    return L"";
  }
  std::wstring result(static_cast<size_t>(wide_len - 1), L'\0');
  MultiByteToWideChar(CP_ACP, 0, tchar_str, -1, result.data(), wide_len);
  return result;
#endif
}

}  // namespace

std::list<CameraInfo> MFAPICapture::EnumerateDevices() {
  std::list<CameraInfo> devices;

  ComMfScope scope;
  if (!scope.ok()) {
    return devices;
  }

  ComPointer<IMFAttributes> config;
  if (FAILED(MFCreateAttributes(&config, 1))) {
    return devices;
  }
  if (FAILED(config->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
                             MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID))) {
    return devices;
  }

  IMFActivate** raw_devices = nullptr;
  UINT32 count = 0;
  if (FAILED(MFEnumDeviceSources(config, &raw_devices, &count)) ||
      !raw_devices) {
    return devices;
  }

  for (UINT32 i = 0; i < count; ++i) {
    WCHAR* name = nullptr;
    if (FAILED(raw_devices[i]->GetAllocatedString(
            MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME, &name, nullptr)) ||
        !name) {
      continue;
    }

    CameraInfo info;
    info.index = static_cast<int>(i);
    info.name = WCharToUtf8(name);
    CoTaskMemFree(name);

    ComPointer<IMFMediaSource> device;
    ComPointer<IMFSourceReader> reader;
    if (FAILED(raw_devices[i]->ActivateObject(IID_PPV_ARGS(&device))) ||
        FAILED(MFCreateSourceReaderFromMediaSource(device, nullptr, &reader))) {
      // 设备被占用时也要把名字和索引返回，界面仍能列出来。
      devices.push_back(std::move(info));
      continue;
    }

    // 一次遍历同时收集分辨率、帧率和像素格式：GetNativeMediaType 在
    // 部分驱动上很慢，分两轮枚举会让设备列表打开明显变卡。
    std::set<FrameSize> frame_sizes;
    int formats = 0;
    ComPointer<IMFMediaType> native_type;
    for (DWORD index = 0;
         SUCCEEDED(reader->GetNativeMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM,
                                              index, &native_type));
         ++index) {
      UINT32 width = 0;
      UINT32 height = 0;
      if (SUCCEEDED(MFGetAttributeSize(native_type, MF_MT_FRAME_SIZE, &width,
                                       &height)) &&
          width > 0 && height > 0) {
        frame_sizes.insert(
            FrameSize{static_cast<int>(width), static_cast<int>(height)});
      }

      UINT32 numerator = 0;
      UINT32 denominator = 0;
      if (SUCCEEDED(MFGetAttributeRatio(native_type, MF_MT_FRAME_RATE, &numerator,
                                        &denominator)) &&
          denominator > 0) {
        const int fps = static_cast<int>(
            (static_cast<double>(numerator) / denominator) + 0.5);
        if (fps > 0) {
          info.supportedFps.insert(fps);
        }
      }

      GUID subtype = GUID_NULL;
      if (SUCCEEDED(native_type->GetGUID(MF_MT_SUBTYPE, &subtype))) {
        if (subtype == MFVideoFormat_NV12) {
          formats |= 1;
        } else if (subtype == MFVideoFormat_YUY2) {
          formats |= 2;
        } else if (subtype == MFVideoFormat_MJPG) {
          formats |= 4;
        } else if (subtype == MFVideoFormat_RGB24) {
          formats |= 8;
        }
      }
    }

    for (const auto& size : frame_sizes) {
      info.supportedFrameSize.push_back(size);
    }
    if (formats & 1) info.supportedFormat.push_back(0);
    if (formats & 2) info.supportedFormat.push_back(1);
    if (formats & 4) info.supportedFormat.push_back(2);
    if (formats & 8) info.supportedFormat.push_back(3);

    devices.push_back(std::move(info));
  }

  ReleaseDeviceArray(raw_devices, count);
  return devices;
}

MFAPICapture::~MFAPICapture() {
  m_pSourceReader.Release();
  m_pDevice.Release();
  if (m_mf_started) {
    MFShutdown();
    m_mf_started = false;
  }
  if (m_com_inited) {
    CoUninitialize();
    m_com_inited = false;
  }
}

std::string MFAPICapture::getCurErr() {
  return m_strErr;
}

bool MFAPICapture::init(const CameraInfo& camera_info, int width, int height,
                        int fps) {
  m_strErr.clear();

  bool size_supported = false;
  for (const auto& size : camera_info.supportedFrameSize) {
    if (size.width == width && size.height == height) {
      size_supported = true;
      break;
    }
  }
  // 少数驱动在枚举阶段不返回原生媒体类型；允许调用方用通用档位继续协商。
  if (!size_supported && !camera_info.supportedFrameSize.empty()) {
    m_strErr = "unsupported frame size " + std::to_string(width) + "x" +
               std::to_string(height);
    return false;
  }
  if (fps <= 0) {
    m_strErr = "invalid fps";
    return false;
  }

  // 允许重复 init：先释放上一次的会话，避免设备被自己占住。
  m_pSourceReader.Release();
  m_pDevice.Release();

  if (!m_com_inited) {
    const HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (hr == S_OK || hr == S_FALSE) {
      m_com_inited = true;
    } else if (hr != RPC_E_CHANGED_MODE) {
      m_strErr = "CoInitializeEx error " + HrToString(hr);
      return false;
    }
    // RPC_E_CHANGED_MODE：当前线程已用别的套间初始化 COM，可以直接用，
    // 但析构时不能配对 CoUninitialize。
  }

  if (!m_mf_started) {
    const HRESULT hr = MFStartup(MF_VERSION);
    if (FAILED(hr)) {
      m_strErr = "MFStartup error " + HrToString(hr);
      return false;
    }
    m_mf_started = true;
  }

  if (!createDevice(camera_info)) {
    if (m_strErr.empty()) m_strErr = "createDevice error";
    return false;
  }

  m_width = width;
  m_height = height;

  if (!setFrameInfo(width, height, fps)) {
    m_pSourceReader.Release();
    m_pDevice.Release();
    return false;
  }

  m_fps = fps;
  m_frame_index = 0;
  m_last_timestamp_us = 0;
  m_first_sample_time = 0;
  return true;
}

bool MFAPICapture::doCapture() {
  if (!m_pSourceReader) {
    m_strErr = "capture not initialized";
    return false;
  }
  if (!m_buffer) {
    m_strErr = "output buffer not set";
    return false;
  }

  const int width = m_width;
  const int height = m_height;
  if (width <= 0 || height <= 0) {
    m_strErr = "invalid frame size";
    return false;
  }

  const size_t y_size = static_cast<size_t>(width) * static_cast<size_t>(height);
  const size_t needed = y_size + y_size / 2;  // NV12: Y + 交错 UV
  if (m_buffer_size != 0 && m_buffer_size < needed) {
    m_strErr = "output buffer too small, need " + std::to_string(needed);
    return false;
  }

  ComPointer<IMFSample> sample;
  ComPointer<IMFMediaBuffer> buffer;

  DWORD flags = 0;
  int ticks = 0;
  for (;;) {
    // ReadSample 的输出参数必须是空指针：上一轮若只收到 STREAMTICK，
    // sample 仍持有旧接口，直接复用会让老样本泄漏。
    sample.Release();

    const HRESULT hr = m_pSourceReader->ReadSample(
        MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, nullptr, &flags, nullptr,
        &sample);
    if (FAILED(hr)) {
      m_strErr = "ReadSample failed " + HrToString(hr);
      return false;
    }
    if (flags & MF_SOURCE_READERF_ERROR) {
      m_strErr = "ReadSample reported device error";
      return false;
    }
    if (flags & MF_SOURCE_READERF_ENDOFSTREAM) {
      m_strErr = "end of stream";
      return false;
    }
    if (flags & MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED) {
      refreshStride();
    }
    // STREAMTICK 只是时间戳通知，没有新帧，继续等。
    if (!(flags & MF_SOURCE_READERF_STREAMTICK) && sample) {
      break;
    }
    // 驱动异常时 ReadSample 可能一直返回 STREAMTICK，避免调用线程永久卡死。
    if (++ticks > 100) {
      m_strErr = "no frame received from device";
      return false;
    }
  }

  if (FAILED(sample->ConvertToContiguousBuffer(&buffer))) {
    m_strErr = "ConvertToContiguousBuffer failed";
    return false;
  }

  BYTE* data = nullptr;
  DWORD max_length = 0;
  DWORD current_length = 0;
  if (FAILED(buffer->Lock(&data, &max_length, &current_length))) {
    m_strErr = "IMFSample buffer Lock failed";
    return false;
  }

  bool ok = false;
  const int src_stride = (m_src_stride > 0) ? m_src_stride : width;
  uint8_t* dst = m_buffer.get();

  if (src_stride == width) {
    // 常见情况：驱动直接给紧凑 NV12，一次拷贝即可。
    const size_t copy_len =
        (std::min)(static_cast<size_t>(current_length), needed);
    memcpy(dst, data, copy_len);
    ok = (copy_len >= needed);
  } else {
    // 驱动按 16/32 字节对齐了行宽，必须逐行拷贝，否则 UV 会整体错位、
    // 画面出现斜纹。输出保持紧凑布局，调用方的缓冲区尺寸约定不变。
    const size_t src_needed = static_cast<size_t>(src_stride) * height +
                              static_cast<size_t>(src_stride) * (height / 2);
    if (current_length >= src_needed) {
      for (int y = 0; y < height; ++y) {
        memcpy(dst + static_cast<size_t>(y) * width,
               data + static_cast<size_t>(y) * src_stride, width);
      }
      const uint8_t* uv_src = data + static_cast<size_t>(src_stride) * height;
      uint8_t* uv_dst = dst + y_size;
      for (int y = 0; y < height / 2; ++y) {
        memcpy(uv_dst + static_cast<size_t>(y) * width,
               uv_src + static_cast<size_t>(y) * src_stride, width);
      }
      ok = true;
    }
  }
  buffer->Unlock();

  if (!ok) {
    m_strErr = "unexpected NV12 frame size (stride=" +
               std::to_string(src_stride) +
               ", length=" + std::to_string(current_length) + ")";
    return false;
  }

  LONGLONG sample_time = 0;
  if (SUCCEEDED(sample->GetSampleTime(&sample_time))) {
    if (m_first_sample_time == 0) {
      m_first_sample_time = sample_time;
    }
    // MF 时间单位是 100ns，转成微秒，供上层做抖动/丢帧统计。
    m_last_timestamp_us =
        static_cast<int64_t>((sample_time - m_first_sample_time) / 10);
  }
  ++m_frame_index;
  return true;
}

bool MFAPICapture::createDevice(const CameraInfo& camera_info) {
  ComPointer<IMFAttributes> config;
  IMFActivate** raw_devices = nullptr;
  UINT32 count = 0;

  if (FAILED(MFCreateAttributes(&config, 1))) {
    m_strErr = "MFCreateAttributes error";
    return false;
  }
  if (FAILED(config->SetGUID(MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
                             MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID))) {
    m_strErr = "SetGUID video capture error";
    return false;
  }
  if (FAILED(MFEnumDeviceSources(config, &raw_devices, &count)) ||
      !raw_devices || count == 0) {
    m_strErr = "MFEnumDeviceSources error";
    ReleaseDeviceArray(raw_devices, count);
    return false;
  }
  if (camera_info.index < 0 ||
      static_cast<UINT32>(camera_info.index) >= count) {
    m_strErr = "camera index out of range";
    ReleaseDeviceArray(raw_devices, count);
    return false;
  }

  const HRESULT hr =
      raw_devices[camera_info.index]->ActivateObject(IID_PPV_ARGS(&m_pDevice));

  ReleaseDeviceArray(raw_devices, count);
  if (FAILED(hr)) {
    m_strErr = "ActivateObject failed " + HrToString(hr);
  }
  return SUCCEEDED(hr);
}

bool MFAPICapture::setFrameInfo(int width, int height, int fps) {
  ComPointer<IMFMediaType> native_type;
  ComPointer<IMFMediaType> type;

  // 低延迟配置：禁用高级视频处理，让设备直接输出 NV12，避免 MF 内部
  // 额外做一次色彩空间转换和帧缓冲。
  ComPointer<IMFAttributes> reader_attrs;
  if (SUCCEEDED(MFCreateAttributes(&reader_attrs, 1))) {
    reader_attrs->SetUINT32(MF_SOURCE_READER_ENABLE_ADVANCED_VIDEO_PROCESSING,
                            FALSE);
  }

  if (FAILED(MFCreateSourceReaderFromMediaSource(m_pDevice, reader_attrs.Get(),
                                                 &m_pSourceReader))) {
    m_strErr = "MFCreateSourceReaderFromMediaSource error";
    return false;
  }

  if (FAILED(m_pSourceReader->GetCurrentMediaType(
          MF_SOURCE_READER_FIRST_VIDEO_STREAM, &native_type))) {
    m_strErr = "GetCurrentMediaType error";
    return false;
  }

  GUID major_type = MFMediaType_Video;
  native_type->GetGUID(MF_MT_MAJOR_TYPE, &major_type);

  if (FAILED(MFCreateMediaType(&type))) {
    m_strErr = "MFCreateMediaType error";
    return false;
  }
  if (FAILED(type->SetGUID(MF_MT_MAJOR_TYPE, major_type))) {
    m_strErr = "MF_MT_MAJOR_TYPE error";
    return false;
  }
  if (FAILED(type->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12))) {
    m_strErr = "MF_MT_SUBTYPE error";
    return false;
  }
  if (FAILED(MFSetAttributeRatio(type, MF_MT_FRAME_RATE,
                                 static_cast<UINT32>(fps), 1))) {
    m_strErr = "MFSetAttributeRatio error";
    return false;
  }
  if (FAILED(MFSetAttributeSize(type, MF_MT_FRAME_SIZE,
                                static_cast<UINT32>(width),
                                static_cast<UINT32>(height)))) {
    m_strErr = "MF_MT_FRAME_SIZE error";
    return false;
  }
  type->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
  type->SetUINT32(MF_MT_ALL_SAMPLES_INDEPENDENT, TRUE);
  MFSetAttributeRatio(type, MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
  // 只是建议值，部分驱动仍会按对齐后的行宽输出，后面 refreshStride 以实际为准。
  type->SetUINT32(MF_MT_DEFAULT_STRIDE, static_cast<UINT32>(width));

  const HRESULT hr = m_pSourceReader->SetCurrentMediaType(
      MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, type);
  if (FAILED(hr)) {
    _com_error error(hr);
    // _com_error::ErrorMessage 返回 TCHAR，先统一成 wchar_t 再转 UTF-8，
    // 保证非 UNICODE 编译配置下也能取到可读错误。
    m_strErr = "SetCurrentMediaType failed: " +
               WCharToUtf8(TCHARToWString(error.ErrorMessage()).c_str()) +
               " (" + HrToString(hr) + ")";
    return false;
  }

  refreshStride();

  ComPointer<IMFMediaType> actual_type;
  if (SUCCEEDED(m_pSourceReader->GetCurrentMediaType(
          MF_SOURCE_READER_FIRST_VIDEO_STREAM, &actual_type))) {
    GUID subtype = GUID_NULL;
    actual_type->GetGUID(MF_MT_SUBTYPE, &subtype);
    UINT32 actual_width = 0;
    UINT32 actual_height = 0;
    MFGetAttributeSize(actual_type, MF_MT_FRAME_SIZE, &actual_width,
                       &actual_height);
    UINT32 numerator = 0;
    UINT32 denominator = 0;
    MFGetAttributeRatio(actual_type, MF_MT_FRAME_RATE, &numerator, &denominator);

    const char* format = "unknown";
    if (subtype == MFVideoFormat_NV12) {
      format = "NV12";
    } else if (subtype == MFVideoFormat_YUY2) {
      format = "YUY2";
    } else if (subtype == MFVideoFormat_MJPG) {
      format = "MJPG";
    } else if (subtype == MFVideoFormat_RGB24) {
      format = "RGB24";
    } else if (subtype == MFVideoFormat_I420) {
      format = "I420";
    }
    RTC_LOG(LS_INFO) << "摄像头协商格式 requested=" << width << "x" << height
                     << " actual=" << actual_width << "x" << actual_height
                     << " " << format << " fps=" << numerator << "/"
                     << denominator << " stride=" << m_src_stride;
  }

  return true;
}

void MFAPICapture::refreshStride() {
  m_src_stride = (m_width > 0) ? m_width : 0;

  ComPointer<IMFMediaType> actual_type;
  if (FAILED(m_pSourceReader->GetCurrentMediaType(
          MF_SOURCE_READER_FIRST_VIDEO_STREAM, &actual_type))) {
    return;
  }

  UINT32 stride = 0;
  if (SUCCEEDED(actual_type->GetUINT32(MF_MT_DEFAULT_STRIDE, &stride)) &&
      stride != 0) {
    // 负值表示 bottom-up，按正的行宽处理（相机基本不会给出这种布局）。
    const int32_t signed_stride = static_cast<int32_t>(stride);
    m_src_stride = (signed_stride < 0) ? -signed_stride : signed_stride;
  }

  if (m_src_stride < m_width) {
    m_src_stride = m_width;
  }

  // 分辨率不在运行期修改：调用方的输出缓冲区是按 init 时的尺寸分配的，
  // 这里擅自改大 m_width 会让 doCapture 直接写越界。驱动拒绝请求时由
  // doCapture 的长度校验报错，交给上层重新 init。
}

}  // namespace camera
}  // namespace talk
