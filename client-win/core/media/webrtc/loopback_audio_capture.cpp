// File: core/media/webrtc/loopback_audio_capture.cpp
// Brief: 实现默认播放设备的 WASAPI loopback 采集与 48 kHz 单声道转换。
#include "media/webrtc/loopback_audio_capture.h"

#include <algorithm>
#include <cmath>

#include <windows.h>
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <mmreg.h>
#include <wrl/client.h>

#include "api/audio/audio_device_defines.h"
#include "rtc_base/logging.h"

namespace talk {
namespace {

constexpr int kTargetSampleRate = 48000;
constexpr int kTargetChannels = 1;
constexpr int kBytesPerSample = 2;
constexpr int kSamplesPerFrame = 480;  // 10 ms @ 48 kHz
constexpr size_t kMaxPendingSamples = kTargetSampleRate / 10;  // 100 ms

}  // namespace

struct LoopbackAudioCapture::Impl {
  enum class Format {
    kUnknown,
    kFloat32,
    kPcm16,
    kPcm32,
  };

  Microsoft::WRL::ComPtr<IAudioClient> client;
  Microsoft::WRL::ComPtr<IAudioCaptureClient> capture;
  Format format = Format::kUnknown;
  uint32_t sample_rate = kTargetSampleRate;
  uint16_t channels = 0;
};

LoopbackAudioCapture::LoopbackAudioCapture() = default;

LoopbackAudioCapture::~LoopbackAudioCapture() { Terminate(); }

bool LoopbackAudioCapture::Init(std::string* error) {
  if (initialized_) {
    return true;
  }
  if (!impl_) {
    impl_ = std::make_unique<Impl>();
  }
  if (!Open(error)) {
    Close();
    return false;
  }
  initialized_ = true;
  RTC_LOG(LS_INFO) << "loopback capture ready: rate=" << impl_->sample_rate
                   << " channels=" << impl_->channels
                   << " format=" << static_cast<int>(impl_->format);
  return true;
}

bool LoopbackAudioCapture::Start(webrtc::AudioTransport* transport,
                                 std::string* error) {
  if (!initialized_ || !impl_ || !impl_->client || !impl_->capture) {
    if (error != nullptr) {
      *error = "系统音采集未初始化";
    }
    return false;
  }
  if (transport == nullptr) {
    if (error != nullptr) {
      *error = "音频回调未注册";
    }
    return false;
  }
  if (running_) {
    return true;
  }
  Stop();
  pending_.clear();
  transport_ = transport;
  stop_ = false;
  const HRESULT hr = impl_->client->Start();
  if (FAILED(hr)) {
    RecordError(hr);
    if (error != nullptr) {
      *error = "启动系统音采集失败";
    }
    return false;
  }
  running_ = true;
  thread_ = std::thread([this] { Run(); });
  return true;
}

void LoopbackAudioCapture::Stop() {
  if (!thread_.joinable()) {
    running_ = false;
    return;
  }
  stop_ = true;
  thread_.join();
  running_ = false;
  pending_.clear();
}

void LoopbackAudioCapture::Terminate() {
  Stop();
  Close();
  initialized_ = false;
}

Json LoopbackAudioCapture::stats() const {
  const uint32_t sample_rate = impl_ ? impl_->sample_rate : 0;
  const uint16_t channels = impl_ ? impl_->channels : 0;
  return {{"initialized", initialized_.load()},
          {"running", running_.load()},
          {"frames", frames_.load()},
          {"bytes", bytes_.load()},
          {"silent_packets", silent_packets_.load()},
          {"rms", rms_.load()},
          {"sample_rate", sample_rate},
          {"channels", channels},
          {"last_error", last_error_.load()}};
}

bool LoopbackAudioCapture::Open(std::string* error) {
  auto fail = [&](const char* message, HRESULT hr) {
    RecordError(hr);
    if (error != nullptr) {
      *error = message;
    }
    RTC_LOG(LS_WARNING) << "loopback: " << message << " hr=" << hr;
    return false;
  };

  Microsoft::WRL::ComPtr<IMMDeviceEnumerator> enumerator;
  HRESULT hr = CoCreateInstance(
      __uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
      IID_PPV_ARGS(enumerator.GetAddressOf()));
  if (FAILED(hr)) {
    return fail("系统音采集无法创建设备枚举器", hr);
  }

  Microsoft::WRL::ComPtr<IMMDevice> device;
  hr = enumerator->GetDefaultAudioEndpoint(eRender, eConsole,
                                           device.GetAddressOf());
  if (FAILED(hr)) {
    return fail("没有可用的默认播放设备", hr);
  }

  hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                        reinterpret_cast<void**>(
                            impl_->client.GetAddressOf()));
  if (FAILED(hr)) {
    return fail("系统音采集无法打开播放设备", hr);
  }

  WAVEFORMATEX* mix = nullptr;
  hr = impl_->client->GetMixFormat(&mix);
  if (FAILED(hr) || mix == nullptr) {
    if (mix != nullptr) {
      CoTaskMemFree(mix);
    }
    return fail("系统音采集无法读取播放格式", hr);
  }

  WORD sub_format = mix->wFormatTag;
  if (sub_format == WAVE_FORMAT_EXTENSIBLE) {
    const auto* extend = reinterpret_cast<const WAVEFORMATEXTENSIBLE*>(mix);
    sub_format = static_cast<WORD>(extend->SubFormat.Data1);
  }
  if (sub_format == WAVE_FORMAT_IEEE_FLOAT && mix->wBitsPerSample == 32) {
    impl_->format = Impl::Format::kFloat32;
  } else if (sub_format == WAVE_FORMAT_PCM && mix->wBitsPerSample == 16) {
    impl_->format = Impl::Format::kPcm16;
  } else if (sub_format == WAVE_FORMAT_PCM && mix->wBitsPerSample == 32) {
    impl_->format = Impl::Format::kPcm32;
  } else {
    const WORD bits = mix->wBitsPerSample;
    const WORD tag = mix->wFormatTag;
    CoTaskMemFree(mix);
    RTC_LOG(LS_WARNING) << "loopback: unsupported mix format tag=" << tag
                        << " bits=" << bits;
    if (error != nullptr) {
      *error = "系统音采集不支持当前播放格式（仅支持 float32/PCM16/PCM32）";
    }
    return false;
  }
  impl_->sample_rate = mix->nSamplesPerSec;
  impl_->channels = mix->nChannels;
  if (impl_->channels == 0 ||
      impl_->sample_rate < 8000 || impl_->sample_rate > 192000) {
    CoTaskMemFree(mix);
    if (error != nullptr) {
      *error = "系统音采集播放格式无效";
    }
    return false;
  }

  hr = impl_->client->Initialize(AUDCLNT_SHAREMODE_SHARED,
                                 AUDCLNT_STREAMFLAGS_LOOPBACK, 0, 0, mix,
                                 nullptr);
  CoTaskMemFree(mix);
  if (FAILED(hr)) {
    return fail("系统音采集无法初始化 loopback", hr);
  }

  hr = impl_->client->GetService(
      IID_PPV_ARGS(impl_->capture.GetAddressOf()));
  if (FAILED(hr)) {
    return fail("系统音采集无法获取 capture 接口", hr);
  }
  return true;
}

void LoopbackAudioCapture::Close() {
  if (impl_) {
    impl_->capture.Reset();
    impl_->client.Reset();
  }
}

void LoopbackAudioCapture::Run() {
  const HRESULT com = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
  if (FAILED(com)) {
    RecordError(com);
    running_ = false;
    return;
  }
  if (!impl_ || !impl_->capture || !impl_->client) {
    running_ = false;
    CoUninitialize();
    return;
  }

  IAudioCaptureClient* capture = impl_->capture.Get();
  while (!stop_) {
    UINT32 packet = 0;
    HRESULT hr = capture->GetNextPacketSize(&packet);
    if (FAILED(hr)) {
      RecordError(hr);
      break;
    }
    while (packet > 0 && !stop_) {
      BYTE* data = nullptr;
      UINT32 frames = 0;
      DWORD flags = 0;
      hr = capture->GetBuffer(&data, &frames, &flags, nullptr, nullptr);
      if (FAILED(hr)) {
        RecordError(hr);
        stop_ = true;
        break;
      }
      if (frames > 0) {
        std::vector<int16_t> mono;
        if (Convert(data, frames, flags, &mono)) {
          Push(mono.data(), mono.size());
        }
      }
      capture->ReleaseBuffer(frames);
      hr = capture->GetNextPacketSize(&packet);
      if (FAILED(hr)) {
        RecordError(hr);
        stop_ = true;
        break;
      }
    }
    Sleep(5);
  }

  if (impl_ && impl_->client) {
    impl_->client->Stop();
  }
  running_ = false;
  CoUninitialize();
}

bool LoopbackAudioCapture::Convert(const uint8_t* data, uint32_t frames,
                                   uint32_t flags,
                                   std::vector<int16_t>* out) {
  if (out == nullptr || !impl_ || impl_->channels == 0) {
    return false;
  }
  out->resize(frames);
  if ((flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0 || data == nullptr) {
    std::fill(out->begin(), out->end(), 0);
    ++silent_packets_;
    return true;
  }

  const size_t channels = impl_->channels;
  switch (impl_->format) {
    case Impl::Format::kFloat32: {
      const float* src = reinterpret_cast<const float*>(data);
      for (uint32_t i = 0; i < frames; ++i) {
        double sum = 0;
        for (size_t channel = 0; channel < channels; ++channel) {
          sum += src[i * channels + channel];
        }
        const double value =
            std::clamp(sum / static_cast<double>(channels) * 32767.0,
                       -32768.0, 32767.0);
        (*out)[i] = static_cast<int16_t>(value);
      }
      break;
    }
    case Impl::Format::kPcm16: {
      const int16_t* src = reinterpret_cast<const int16_t*>(data);
      for (uint32_t i = 0; i < frames; ++i) {
        int32_t sum = 0;
        for (size_t channel = 0; channel < channels; ++channel) {
          sum += src[i * channels + channel];
        }
        (*out)[i] = static_cast<int16_t>(sum / static_cast<int32_t>(channels));
      }
      break;
    }
    case Impl::Format::kPcm32: {
      const int32_t* src = reinterpret_cast<const int32_t*>(data);
      for (uint32_t i = 0; i < frames; ++i) {
        int64_t sum = 0;
        for (size_t channel = 0; channel < channels; ++channel) {
          sum += src[i * channels + channel];
        }
        (*out)[i] = static_cast<int16_t>(sum / static_cast<int64_t>(channels) >>
                                         16);
      }
      break;
    }
    default:
      return false;
  }
  return true;
}

void LoopbackAudioCapture::Push(const int16_t* samples, size_t count) {
  if (samples == nullptr || count == 0) {
    return;
  }
  if (impl_->sample_rate == kTargetSampleRate) {
    pending_.insert(pending_.end(), samples, samples + count);
  } else {
    const double ratio =
        static_cast<double>(kTargetSampleRate) / impl_->sample_rate;
    const size_t out_count = static_cast<size_t>(count * ratio);
    pending_.reserve(pending_.size() + out_count);
    for (size_t i = 0; i < out_count; ++i) {
      const double position = static_cast<double>(i) / ratio;
      const size_t index = static_cast<size_t>(position);
      const double fraction = position - static_cast<double>(index);
      const int16_t first = samples[std::min(index, count - 1)];
      const int16_t second = samples[std::min(index + 1, count - 1)];
      pending_.push_back(static_cast<int16_t>(
          static_cast<double>(first) +
          (static_cast<double>(second) - first) * fraction));
    }
  }
  if (pending_.size() > kMaxPendingSamples) {
    pending_.erase(pending_.begin(),
                   pending_.begin() + (pending_.size() - kMaxPendingSamples));
  }
  PushPending();
}

void LoopbackAudioCapture::PushPending() {
  size_t offset = 0;
  auto* transport = transport_.load();
  while (pending_.size() - offset >= kSamplesPerFrame) {
    if (transport == nullptr) {
      break;
    }
    int16_t frame[kSamplesPerFrame];
    std::copy_n(pending_.begin() + offset, kSamplesPerFrame, frame);
    uint32_t level = 0;
    transport->RecordedDataIsAvailable(
        frame, kSamplesPerFrame, kBytesPerSample, kTargetChannels,
        kTargetSampleRate, 0, 0, 0, false, level);
    ++frames_;
    bytes_ += kSamplesPerFrame * kBytesPerSample;
    double energy = 0;
    for (const int16_t sample : frame) {
      energy += static_cast<double>(sample) * sample;
    }
    rms_ = static_cast<int>(std::sqrt(energy / kSamplesPerFrame));
    offset += kSamplesPerFrame;
  }
  if (offset > 0) {
    pending_.erase(pending_.begin(), pending_.begin() + offset);
  }
}

void LoopbackAudioCapture::RecordError(long hr) {
  last_error_ = hr;
  RTC_LOG(LS_WARNING) << "loopback capture error: hr=" << hr;
}

}  // namespace talk
