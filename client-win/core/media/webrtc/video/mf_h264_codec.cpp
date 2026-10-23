// File: core/media/webrtc/video/mf_h264_codec.cpp
// Brief: 实现 Media Foundation H.264 编解码器封装。
#include "media/webrtc/video/mf_h264_codec.h"

#include <windows.h>

#include <codecapi.h>
#include <mfapi.h>
#include <mferror.h>
#include <mfidl.h>
#include <mftransform.h>
// wmcodecdsp.h 会带入 strmif.h，其中已定义 ICodecAPI 与 CodecAPIEventData；
// 不能再单独包含 icodecapi.h，否则结构体会重定义。
#include <wmcodecdsp.h>
#include <wrl/client.h>

#include <algorithm>
#include <cstring>
#include <deque>
#include <mutex>
#include <utility>

#include "media/webrtc/video/h264_bitstream.h"
#include "rtc_base/logging.h"
#include "third_party/libyuv/include/libyuv/convert.h"

namespace talk {
namespace {

using Microsoft::WRL::ComPtr;

constexpr int64_t kHundredNsPerUs = 10;
constexpr int kDefaultFps = 25;

void LogHresult(const char* what, HRESULT hr) {
  RTC_LOG(LS_WARNING) << "MF H.264 " << what << " 失败 hr=0x" << std::hex
                      << static_cast<unsigned long>(hr) << std::dec;
}

// 解码/编码线程未必初始化过 COM；MTA 与 WebRTC 线程模型一致。
void EnsureCom() {
  const HRESULT hr = ::CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  if (FAILED(hr) && hr != RPC_E_CHANGED_MODE) {
    LogHresult("CoInitializeEx", hr);
  }
}

bool SetCodecUint(ICodecAPI* api, const GUID& id, uint32_t value) {
  if (api == nullptr) {
    return false;
  }
  VARIANT var;
  VariantInit(&var);
  var.vt = VT_UI4;
  var.ulVal = value;
  return SUCCEEDED(api->SetValue(&id, &var));
}

bool SetCodecBool(ICodecAPI* api, const GUID& id, bool value) {
  if (api == nullptr) {
    return false;
  }
  VARIANT var;
  VariantInit(&var);
  var.vt = VT_BOOL;
  var.boolVal = value ? VARIANT_TRUE : VARIANT_FALSE;
  return SUCCEEDED(api->SetValue(&id, &var));
}

// 把紧凑 NV12 拷进 MF 的 2D 缓冲（行跨距由 MF 决定，可能大于 width）。
bool FillNv12(IMFMediaBuffer* buffer, int width, int height,
              const uint8_t* nv12, int stride) {
  ComPtr<IMF2DBuffer> buffer_2d;
  if (SUCCEEDED(buffer->QueryInterface(IID_PPV_ARGS(&buffer_2d)))) {
    BYTE* scan0 = nullptr;
    LONG pitch = 0;
    if (FAILED(buffer_2d->Lock2D(&scan0, &pitch))) {
      return false;
    }
    for (int y = 0; y < height; ++y) {
      std::memcpy(scan0 + static_cast<ptrdiff_t>(y) * pitch,
                  nv12 + static_cast<ptrdiff_t>(y) * stride, width);
    }
    for (int y = 0; y < height / 2; ++y) {
      std::memcpy(scan0 + static_cast<ptrdiff_t>(height + y) * pitch,
                  nv12 + static_cast<ptrdiff_t>(height + y) * stride, width);
    }
    buffer_2d->Unlock2D();
    return true;
  }
  BYTE* data = nullptr;
  DWORD max_length = 0;
  DWORD length = 0;
  if (FAILED(buffer->Lock(&data, &max_length, &length))) {
    return false;
  }
  const size_t plane = static_cast<size_t>(width) * height;
  if (max_length < plane * 3 / 2) {
    buffer->Unlock();
    return false;
  }
  std::memcpy(data, nv12, plane);
  std::memcpy(data + plane, nv12 + plane, plane / 2);
  buffer->Unlock();
  return true;
}

// NV12 -> 紧凑 I420。源图可能带宏块对齐边距，按 dst 尺寸左上裁剪。
bool CopyNv12ToI420(const uint8_t* src, int src_stride, int width, int height,
                    int dst_width, int dst_height,
                    std::vector<uint8_t>* i420) {
  if (src == nullptr || i420 == nullptr || width <= 0 || height <= 0 ||
      dst_width <= 0 || dst_height <= 0 || dst_width > width ||
      dst_height > height || (dst_width & 1) != 0 || (dst_height & 1) != 0) {
    return false;
  }
  i420->resize(static_cast<size_t>(dst_width) * dst_height * 3 / 2);
  uint8_t* dst_y = i420->data();
  uint8_t* dst_u = dst_y + static_cast<size_t>(dst_width) * dst_height;
  uint8_t* dst_v = dst_u + static_cast<size_t>(dst_width / 2) * (dst_height / 2);
  libyuv::NV12ToI420(src, src_stride, src + static_cast<size_t>(src_stride) * height,
                     src_stride, dst_y, dst_width, dst_u, dst_width / 2, dst_v,
                     dst_width / 2, dst_width, dst_height);
  return true;
}

}  // namespace

bool EnsureMediaFoundation() {
  static std::once_flag once;
  static HRESULT result = E_FAIL;
  std::call_once(once, [] { result = ::MFStartup(MF_VERSION, MFSTARTUP_LITE); });
  return SUCCEEDED(result);
}

// ---------------------------------------------------------------------------
// 编码器
// ---------------------------------------------------------------------------

class MfH264Encoder::Impl {
 public:
  ~Impl() { Close(); }

  bool Open(int width, int height, int fps, int bitrate_bps) {
    Close();
    if (width <= 0 || height <= 0 || fps <= 0 || bitrate_bps <= 0) {
      return false;
    }
    // NV12 的 UV 平面要求宽高为偶数。
    if ((width & 1) != 0 || (height & 1) != 0) {
      return false;
    }
    if (!EnsureMediaFoundation()) {
      return false;
    }
    EnsureCom();

    ComPtr<IMFTransform> transform;
    HRESULT hr = ::CoCreateInstance(CLSID_CMSH264EncoderMFT, nullptr,
                                    CLSCTX_INPROC_SERVER,
                                    IID_PPV_ARGS(&transform));
    if (FAILED(hr)) {
      LogHresult("编码器创建", hr);
      return false;
    }
    ComPtr<IMFAttributes> attributes;
    if (SUCCEEDED(transform->GetAttributes(&attributes)) && attributes) {
      // MF 的 H.264 编码器是异步 MFT，必须解锁后才能直接 ProcessInput。
      attributes->SetUINT32(MF_TRANSFORM_ASYNC_UNLOCK, TRUE);
    }

    ComPtr<IMFMediaType> output;
    if (FAILED(::MFCreateMediaType(&output))) {
      return false;
    }
    output->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    output->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
    output->SetUINT32(MF_MT_AVG_BITRATE, static_cast<UINT32>(bitrate_bps));
    output->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    output->SetUINT32(MF_MT_MPEG2_PROFILE, eAVEncH264VProfile_ConstrainedBase);
    ::MFSetAttributeSize(output.Get(), MF_MT_FRAME_SIZE, width, height);
    ::MFSetAttributeRatio(output.Get(), MF_MT_FRAME_RATE, fps, 1);
    ::MFSetAttributeRatio(output.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
    hr = transform->SetOutputType(0, output.Get(), 0);
    if (FAILED(hr)) {
      LogHresult("编码器 SetOutputType", hr);
      return false;
    }

    ComPtr<IMFMediaType> input;
    if (FAILED(::MFCreateMediaType(&input))) {
      return false;
    }
    input->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    input->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12);
    input->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    ::MFSetAttributeSize(input.Get(), MF_MT_FRAME_SIZE, width, height);
    ::MFSetAttributeRatio(input.Get(), MF_MT_FRAME_RATE, fps, 1);
    ::MFSetAttributeRatio(input.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
    hr = transform->SetInputType(0, input.Get(), 0);
    if (FAILED(hr)) {
      LogHresult("编码器 SetInputType", hr);
      return false;
    }

    ComPtr<ICodecAPI> codec;
    if (SUCCEEDED(transform.As(&codec))) {
      codec_ = codec;
    }

    width_ = width;
    height_ = height;
    fps_ = fps;
    bitrate_ = bitrate_bps;
    ApplyCodecOptions();

    ComPtr<IMFMediaEventGenerator> events;
    if (SUCCEEDED(transform.As(&events))) {
      events_ = events;
    }
    transform->ProcessMessage(MFT_MESSAGE_COMMAND_FLUSH, 0);
    transform->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0);
    transform->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0);
    transform_ = transform;
    next_timestamp_ = 0;
    sps_.clear();
    pps_.clear();
    RTC_LOG(LS_INFO) << "MF H.264 编码器就绪 size=" << width << "x" << height
                     << " fps=" << fps << " bitrate=" << bitrate_bps;
    return true;
  }

  void Close() {
    if (transform_) {
      transform_->ProcessMessage(MFT_MESSAGE_NOTIFY_END_OF_STREAM, 0);
      transform_->ProcessMessage(MFT_MESSAGE_COMMAND_FLUSH, 0);
    }
    events_.Reset();
    codec_.Reset();
    transform_.Reset();
    width_ = 0;
    height_ = 0;
    fps_ = 0;
    bitrate_ = 0;
    next_timestamp_ = 0;
    sps_.clear();
    pps_.clear();
    ready_.clear();
  }

  bool opened() const { return transform_ != nullptr; }
  int width() const { return width_; }
  int height() const { return height_; }

  bool SetBitrate(int bitrate_bps) {
    if (!transform_ || bitrate_bps <= 0) {
      return false;
    }
    bitrate_ = bitrate_bps;
    return SetCodecUint(codec_.Get(), CODECAPI_AVEncCommonMeanBitRate,
                        static_cast<uint32_t>(bitrate_bps));
  }

  bool Encode(const uint8_t* nv12, int stride, int64_t timestamp_us,
              bool force_key, std::string* out, bool* keyframe) {
    if (!transform_ || nv12 == nullptr || out == nullptr ||
        keyframe == nullptr || stride < width_) {
      return false;
    }
    out->clear();
    *keyframe = false;

    // 已编码但还没交付的帧放在 ready_ 里；这里先把上一轮积压的输出取空，
    // 否则 MFT 会以 MF_E_NOTACCEPTING 拒绝新的输入。
    if (!DrainOutputs()) {
      return false;
    }

    ComPtr<IMFSample> sample;
    ComPtr<IMFMediaBuffer> buffer;
    HRESULT hr = ::MFCreateSample(&sample);
    if (FAILED(hr)) {
      return false;
    }
    hr = ::MFCreate2DMediaBuffer(static_cast<DWORD>(width_),
                                 static_cast<DWORD>(height_),
                                 MFVideoFormat_NV12.Data1, FALSE, &buffer);
    if (FAILED(hr)) {
      LogHresult("MFCreate2DMediaBuffer", hr);
      return false;
    }
    if (!FillNv12(buffer.Get(), width_, height_, nv12, stride)) {
      return false;
    }
    buffer->SetCurrentLength(
        static_cast<DWORD>(static_cast<size_t>(width_) * height_ * 3 / 2));
    sample->AddBuffer(buffer.Get());

    int64_t stamp = timestamp_us * kHundredNsPerUs;
    if (stamp <= next_timestamp_) {
      stamp = next_timestamp_ + 1;
    }
    next_timestamp_ = stamp;
    sample->SetSampleTime(stamp);
    sample->SetSampleDuration(10000000LL / fps_);
    if (force_key) {
      ForceKeyFrame();
    }

    bool accepted = false;
    for (int attempt = 0; attempt < 60 && !accepted; ++attempt) {
      hr = transform_->ProcessInput(0, sample.Get(), 0);
      if (SUCCEEDED(hr)) {
        accepted = true;
        break;
      }
      if (hr != MF_E_NOTACCEPTING) {
        LogHresult("编码器 ProcessInput", hr);
        return false;
      }
      DrainEvents();
      if (!DrainOutputs()) {
        return false;
      }
      ::Sleep(1);
    }
    if (!accepted) {
      // 编码器持续不收：丢这一帧，下一帧重新尝试。
      RTC_LOG(LS_WARNING) << "MF H.264 编码器丢弃一帧";
      return true;
    }

    // 低延迟模式下当前帧通常已经可以取；取不到就留到下一次 Encode。
    if (!DrainOutputs()) {
      return false;
    }
    if (!ready_.empty()) {
      *out = std::move(ready_.front().first);
      *keyframe = ready_.front().second;
      ready_.pop_front();
    }
    return true;
  }

 private:
  void ApplyCodecOptions() {
    ICodecAPI* api = codec_.Get();
    SetCodecUint(api, CODECAPI_AVEncCommonRateControlMode,
                 eAVEncCommonRateControlMode_CBR);
    SetCodecUint(api, CODECAPI_AVEncCommonMeanBitRate,
                 static_cast<uint32_t>(bitrate_));
    SetCodecUint(api, CODECAPI_AVEncMPVGOPSize,
                 static_cast<uint32_t>(fps_ * 2));
    SetCodecUint(api, CODECAPI_AVEncMPVDefaultBPictureCount, 0);
    SetCodecBool(api, CODECAPI_AVLowLatencyMode, true);
    SetCodecBool(api, CODECAPI_AVEncH264CABACEnable, false);
  }

  void ForceKeyFrame() {
    ICodecAPI* api = codec_.Get();
    if (api == nullptr) {
      return;
    }
    VARIANT var;
    VariantInit(&var);
    var.vt = VT_BOOL;
    var.boolVal = VARIANT_TRUE;
    if (api->IsSupported(&CODECAPI_AVEncVideoForceKeyFrame) == S_OK) {
      api->SetValue(&CODECAPI_AVEncVideoForceKeyFrame, &var);
    }
  }

  void DrainEvents() {
    if (!events_) {
      return;
    }
    for (;;) {
      ComPtr<IMFMediaEvent> event;
      if (events_->GetEvent(MF_EVENT_FLAG_NO_WAIT, &event) != S_OK) {
        return;
      }
    }
  }

  // 把 MFT 里所有可取的输出追加到 ready_，保持编码顺序。
  bool DrainOutputs() {
    MFT_OUTPUT_STREAM_INFO info{};
    if (FAILED(transform_->GetOutputStreamInfo(0, &info))) {
      return false;
    }
    for (;;) {
      MFT_OUTPUT_DATA_BUFFER output{};
      ComPtr<IMFSample> self_sample;
      const bool mft_provides =
          (info.dwFlags & MFT_OUTPUT_STREAM_PROVIDES_SAMPLES) != 0;
      if (!mft_provides) {
        if (FAILED(::MFCreateSample(&self_sample))) {
          return false;
        }
        ComPtr<IMFMediaBuffer> memory;
        const DWORD size = info.cbSize > 0
                               ? info.cbSize
                               : static_cast<DWORD>(width_ * height_ * 3 / 2);
        if (FAILED(::MFCreateMemoryBuffer(size, &memory))) {
          return false;
        }
        self_sample->AddBuffer(memory.Get());
        output.pSample = self_sample.Get();
      }
      DWORD status = 0;
      const HRESULT hr = transform_->ProcessOutput(0, 1, &output, &status);
      if (output.pEvents != nullptr) {
        output.pEvents->Release();
      }
      if (hr == MF_E_TRANSFORM_NEED_MORE_INPUT) {
        return true;
      }
      if (hr == MF_E_TRANSFORM_STREAM_CHANGE) {
        ComPtr<IMFMediaType> available;
        if (FAILED(transform_->GetOutputAvailableType(0, 0, &available))) {
          return false;
        }
        if (FAILED(transform_->SetOutputType(0, available.Get(), 0))) {
          return false;
        }
        continue;
      }
      if (FAILED(hr)) {
        LogHresult("编码器 ProcessOutput", hr);
        return false;
      }
      ComPtr<IMFSample> sample = output.pSample;
      if (!sample) {
        return true;
      }
      ComPtr<IMFMediaBuffer> contiguous;
      if (FAILED(sample->ConvertToContiguousBuffer(&contiguous))) {
        return false;
      }
      BYTE* data = nullptr;
      DWORD max_length = 0;
      DWORD length = 0;
      if (FAILED(contiguous->Lock(&data, &max_length, &length))) {
        return false;
      }
      std::string annexb = ToAnnexB(data, length);
      contiguous->Unlock();
      if (annexb.empty()) {
        return false;
      }
      CollectParameterSets(annexb, &sps_, &pps_);
      const bool idr = ContainsNalType(annexb, 5);
      // RTP 不带 sprop-parameter-sets，关键帧必须自带 SPS/PPS。
      if (idr && !ContainsNalType(annexb, 7) && !sps_.empty()) {
        annexb = sps_ + pps_ + annexb;
      }
      ready_.emplace_back(std::move(annexb), idr);
    }
  }

  ComPtr<IMFTransform> transform_;
  ComPtr<ICodecAPI> codec_;
  ComPtr<IMFMediaEventGenerator> events_;
  int width_ = 0;
  int height_ = 0;
  int fps_ = 0;
  int bitrate_ = 0;
  int64_t next_timestamp_ = 0;
  std::string sps_;
  std::string pps_;
  std::deque<std::pair<std::string, bool>> ready_;
};

MfH264Encoder::MfH264Encoder() : impl_(std::make_unique<Impl>()) {}
MfH264Encoder::~MfH264Encoder() = default;

bool MfH264Encoder::Open(int width, int height, int fps, int bitrate_bps) {
  return impl_->Open(width, height, fps, bitrate_bps);
}
void MfH264Encoder::Close() { impl_->Close(); }
bool MfH264Encoder::opened() const { return impl_->opened(); }
int MfH264Encoder::width() const { return impl_->width(); }
int MfH264Encoder::height() const { return impl_->height(); }
bool MfH264Encoder::Encode(const uint8_t* nv12, int stride,
                           int64_t timestamp_us, bool force_key,
                           std::string* out, bool* keyframe) {
  return impl_->Encode(nv12, stride, timestamp_us, force_key, out, keyframe);
}
bool MfH264Encoder::SetBitrate(int bitrate_bps) {
  return impl_->SetBitrate(bitrate_bps);
}

// ---------------------------------------------------------------------------
// 解码器
// ---------------------------------------------------------------------------

class MfH264Decoder::Impl {
 public:
  ~Impl() { Close(); }

  void Close() {
    if (transform_) {
      transform_->ProcessMessage(MFT_MESSAGE_NOTIFY_END_OF_STREAM, 0);
      transform_->ProcessMessage(MFT_MESSAGE_COMMAND_FLUSH, 0);
    }
    events_.Reset();
    transform_.Reset();
    width_ = 0;
    height_ = 0;
    next_timestamp_ = 0;
  }

  bool Decode(const uint8_t* data, size_t size, int64_t timestamp_us,
              std::vector<uint8_t>* i420, int* width, int* height) {
    if (data == nullptr || size == 0 || i420 == nullptr || width == nullptr ||
        height == nullptr) {
      return false;
    }
    std::string annexb = ToAnnexB(data, size);
    if (annexb.empty()) {
      return false;
    }
    int sps_width = 0;
    int sps_height = 0;
    const bool has_sps = ParseSpsSize(annexb, &sps_width, &sps_height);
    if (transform_ == nullptr || (has_sps && (sps_width != width_ ||
                                              sps_height != height_))) {
      if (!has_sps) {
        // 中途加入时先等到带 SPS 的关键帧。
        return false;
      }
      if (!Open(sps_width, sps_height)) {
        return false;
      }
    }

    ComPtr<IMFSample> sample;
    ComPtr<IMFMediaBuffer> buffer;
    if (FAILED(::MFCreateSample(&sample)) ||
        FAILED(::MFCreateMemoryBuffer(static_cast<DWORD>(annexb.size()),
                                      &buffer))) {
      return false;
    }
    BYTE* dst = nullptr;
    DWORD max_length = 0;
    if (FAILED(buffer->Lock(&dst, &max_length, nullptr))) {
      return false;
    }
    std::memcpy(dst, annexb.data(), annexb.size());
    buffer->Unlock();
    buffer->SetCurrentLength(static_cast<DWORD>(annexb.size()));
    sample->AddBuffer(buffer.Get());

    int64_t stamp = timestamp_us * kHundredNsPerUs;
    if (stamp <= next_timestamp_) {
      stamp = next_timestamp_ + 1;
    }
    next_timestamp_ = stamp;
    sample->SetSampleTime(stamp);
    sample->SetSampleDuration(10000000LL / kDefaultFps);

    bool accepted = false;
    for (int attempt = 0; attempt < 60 && !accepted; ++attempt) {
      const HRESULT hr = transform_->ProcessInput(0, sample.Get(), 0);
      if (SUCCEEDED(hr)) {
        accepted = true;
        break;
      }
      if (hr != MF_E_NOTACCEPTING) {
        LogHresult("解码器 ProcessInput", hr);
        return false;
      }
      DrainEvents();
      bool got = false;
      if (!DrainFrames(i420, width, height, &got)) {
        return false;
      }
      if (got) {
        return true;
      }
      ::Sleep(1);
    }
    if (!accepted) {
      return false;
    }
    bool got = false;
    if (!DrainFrames(i420, width, height, &got)) {
      return false;
    }
    // 只有真正解出一帧才返回 true；重排序/延迟期间没有输出是正常的。
    // 上层据此决定是否构造 VideoFrame，否则会回调 0×0 的空帧。
    return got;
  }

 private:
  bool Open(int width, int height) {
    Close();
    if (!EnsureMediaFoundation()) {
      return false;
    }
    EnsureCom();
    ComPtr<IMFTransform> transform;
    HRESULT hr = ::CoCreateInstance(CLSID_CMSH264DecoderMFT, nullptr,
                                    CLSCTX_INPROC_SERVER,
                                    IID_PPV_ARGS(&transform));
    if (FAILED(hr)) {
      LogHresult("解码器创建", hr);
      return false;
    }
    ComPtr<IMFAttributes> attributes;
    if (SUCCEEDED(transform->GetAttributes(&attributes)) && attributes) {
      attributes->SetUINT32(MF_LOW_LATENCY, TRUE);
      attributes->SetUINT32(MF_TRANSFORM_ASYNC_UNLOCK, TRUE);
    }

    ComPtr<IMFMediaType> input;
    if (FAILED(::MFCreateMediaType(&input))) {
      return false;
    }
    input->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    input->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_H264);
    ::MFSetAttributeSize(input.Get(), MF_MT_FRAME_SIZE, width, height);
    hr = transform->SetInputType(0, input.Get(), 0);
    if (FAILED(hr)) {
      LogHresult("解码器 SetInputType", hr);
      return false;
    }

    ComPtr<IMFMediaType> output;
    if (FAILED(::MFCreateMediaType(&output))) {
      return false;
    }
    output->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
    output->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_NV12);
    output->SetUINT32(MF_MT_INTERLACE_MODE, MFVideoInterlace_Progressive);
    ::MFSetAttributeSize(output.Get(), MF_MT_FRAME_SIZE, width, height);
    ::MFSetAttributeRatio(output.Get(), MF_MT_FRAME_RATE, kDefaultFps, 1);
    ::MFSetAttributeRatio(output.Get(), MF_MT_PIXEL_ASPECT_RATIO, 1, 1);
    hr = transform->SetOutputType(0, output.Get(), 0);
    if (FAILED(hr)) {
      // 有些版本要求用系统给出的可用类型。
      ComPtr<IMFMediaType> available;
      if (FAILED(transform->GetOutputAvailableType(0, 0, &available)) ||
          FAILED(transform->SetOutputType(0, available.Get(), 0))) {
        LogHresult("解码器 SetOutputType", hr);
        return false;
      }
    }

    ComPtr<IMFMediaEventGenerator> events;
    if (SUCCEEDED(transform.As(&events))) {
      events_ = events;
    }
    transform->ProcessMessage(MFT_MESSAGE_COMMAND_FLUSH, 0);
    transform->ProcessMessage(MFT_MESSAGE_NOTIFY_BEGIN_STREAMING, 0);
    transform->ProcessMessage(MFT_MESSAGE_NOTIFY_START_OF_STREAM, 0);
    transform_ = transform;
    width_ = width;
    height_ = height;
    next_timestamp_ = 0;
    RTC_LOG(LS_INFO) << "MF H.264 解码器就绪 size=" << width << "x" << height;
    return true;
  }

  void DrainEvents() {
    if (!events_) {
      return;
    }
    for (;;) {
      ComPtr<IMFMediaEvent> event;
      if (events_->GetEvent(MF_EVENT_FLAG_NO_WAIT, &event) != S_OK) {
        return;
      }
    }
  }

  bool DrainFrames(std::vector<uint8_t>* i420, int* width, int* height,
                   bool* got) {
    *got = false;
    MFT_OUTPUT_STREAM_INFO info{};
    if (FAILED(transform_->GetOutputStreamInfo(0, &info))) {
      return false;
    }
    for (;;) {
      MFT_OUTPUT_DATA_BUFFER output{};
      ComPtr<IMFSample> self_sample;
      const bool mft_provides =
          (info.dwFlags & MFT_OUTPUT_STREAM_PROVIDES_SAMPLES) != 0;
      if (!mft_provides) {
        if (FAILED(::MFCreateSample(&self_sample))) {
          return false;
        }
        ComPtr<IMFMediaBuffer> memory;
        const DWORD size = info.cbSize > 0
                               ? info.cbSize
                               : static_cast<DWORD>(width_ * height_ * 3 / 2);
        if (FAILED(::MFCreateMemoryBuffer(size, &memory))) {
          return false;
        }
        self_sample->AddBuffer(memory.Get());
        output.pSample = self_sample.Get();
      }
      DWORD status = 0;
      const HRESULT hr = transform_->ProcessOutput(0, 1, &output, &status);
      if (output.pEvents != nullptr) {
        output.pEvents->Release();
      }
      if (hr == MF_E_TRANSFORM_NEED_MORE_INPUT) {
        return true;
      }
      if (hr == MF_E_TRANSFORM_STREAM_CHANGE) {
        ComPtr<IMFMediaType> available;
        if (FAILED(transform_->GetOutputAvailableType(0, 0, &available)) ||
            FAILED(transform_->SetOutputType(0, available.Get(), 0))) {
          return false;
        }
        continue;
      }
      if (FAILED(hr)) {
        LogHresult("解码器 ProcessOutput", hr);
        return false;
      }
      ComPtr<IMFSample> sample = output.pSample;
      if (!sample) {
        return true;
      }

      int frame_width = width_;
      int frame_height = height_;
      ComPtr<IMFMediaType> current;
      if (SUCCEEDED(transform_->GetOutputCurrentType(0, &current))) {
        UINT32 size_width = 0;
        UINT32 size_height = 0;
        if (SUCCEEDED(::MFGetAttributeSize(current.Get(), MF_MT_FRAME_SIZE,
                                           &size_width, &size_height)) &&
            size_width > 0 && size_height > 0) {
          frame_width = static_cast<int>(size_width);
          frame_height = static_cast<int>(size_height);
        }
      }

      ComPtr<IMFMediaBuffer> contiguous;
      if (FAILED(sample->ConvertToContiguousBuffer(&contiguous))) {
        return false;
      }
      // 解码器输出按宏块对齐（如 640x360 编为 640x368），
      // 这里按 SPS 显示尺寸做左上裁剪，上层拿到的就是真实分辨率。
      const int dst_width =
          (width_ > 0 && width_ <= frame_width) ? width_ : frame_width;
      const int dst_height =
          (height_ > 0 && height_ <= frame_height) ? height_ : frame_height;
      bool copied = false;
      ComPtr<IMF2DBuffer> buffer_2d;
      if (SUCCEEDED(contiguous.As(&buffer_2d))) {
        BYTE* scan0 = nullptr;
        LONG pitch = 0;
        if (FAILED(buffer_2d->Lock2D(&scan0, &pitch))) {
          return false;
        }
        copied = CopyNv12ToI420(scan0, pitch, frame_width, frame_height,
                                dst_width, dst_height, i420);
        buffer_2d->Unlock2D();
      } else {
        BYTE* data = nullptr;
        DWORD max_length = 0;
        DWORD length = 0;
        if (FAILED(contiguous->Lock(&data, &max_length, &length))) {
          return false;
        }
        copied = CopyNv12ToI420(data, frame_width, frame_width, frame_height,
                                dst_width, dst_height, i420);
        contiguous->Unlock();
      }
      if (!copied) {
        return false;
      }
      *width = dst_width;
      *height = dst_height;
      *got = true;
      return true;
    }
  }

  ComPtr<IMFTransform> transform_;
  ComPtr<IMFMediaEventGenerator> events_;
  int width_ = 0;
  int height_ = 0;
  int64_t next_timestamp_ = 0;
};

MfH264Decoder::MfH264Decoder() : impl_(std::make_unique<Impl>()) {}
MfH264Decoder::~MfH264Decoder() = default;

void MfH264Decoder::Close() { impl_->Close(); }
bool MfH264Decoder::Decode(const uint8_t* data, size_t size,
                           int64_t timestamp_us, std::vector<uint8_t>* i420,
                           int* width, int* height) {
  return impl_->Decode(data, size, timestamp_us, i420, width, height);
}

}  // namespace talk
