#include "rtcwin/webrtc_media/loopback_audio_capture.h"

#include "api/audio/audio_device_defines.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include <windows.h>
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <mmreg.h>
#include <wrl/client.h>

namespace rtcwin::webrtc_media {
namespace {

constexpr int kTargetSampleRate = 48000;
constexpr int kTargetChannels = 1;
constexpr int kBytesPerSample = 2;
constexpr int kSamplesPerFrame = 480;
constexpr std::size_t kMaxPendingSamples = kTargetSampleRate / 10;
constexpr int kDeviceCheckIterations = 400;

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
  std::atomic<std::uint32_t> sample_rate{kTargetSampleRate};
  std::atomic<std::uint16_t> channels{0};
  std::wstring device_id;
};

bool DefaultRenderDeviceId(std::wstring* out) {
  Microsoft::WRL::ComPtr<IMMDeviceEnumerator> enumerator;
  if (FAILED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
                              IID_PPV_ARGS(enumerator.GetAddressOf())))) {
    return false;
  }
  Microsoft::WRL::ComPtr<IMMDevice> device;
  if (FAILED(enumerator->GetDefaultAudioEndpoint(eRender, eConsole,
                                                 device.GetAddressOf()))) {
    return false;
  }
  LPWSTR id = nullptr;
  if (FAILED(device->GetId(&id)) || id == nullptr) {
    return false;
  }
  out->assign(id);
  CoTaskMemFree(id);
  return true;
}

LoopbackAudioCapture::LoopbackAudioCapture() = default;
LoopbackAudioCapture::~LoopbackAudioCapture() { Terminate(); }

bool LoopbackAudioCapture::Init(std::string* error) {
  if (initialized_) return true;
  if (!impl_) impl_ = std::make_unique<Impl>();
  if (!Open(error)) {
    Close();
    return false;
  }
  initialized_ = true;
  return true;
}

bool LoopbackAudioCapture::Start(webrtc::AudioTransport* transport,
                                 std::string* error) {
  if (!initialized_ || !impl_ || !impl_->client || !impl_->capture) {
    if (error) *error = "系统音采集未初始化";
    return false;
  }
  if (transport == nullptr) {
    if (error) *error = "音频回调未注册";
    return false;
  }
  if (running_) return true;
  Stop();
  pending_.clear();
  transport_ = transport;
  stop_ = false;
  const HRESULT hr = impl_->client->Start();
  if (FAILED(hr)) {
    RecordError(hr);
    if (error) *error = "启动系统音采集失败";
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

bool LoopbackAudioCapture::Open(std::string* error) {
  auto fail = [&](const char* message, HRESULT hr) {
    RecordError(hr);
    if (error) *error = message;
    return false;
  };

  Microsoft::WRL::ComPtr<IMMDeviceEnumerator> enumerator;
  HRESULT hr = CoCreateInstance(
      __uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
      IID_PPV_ARGS(enumerator.GetAddressOf()));
  if (FAILED(hr)) return fail("系统音采集无法创建设备枚举器", hr);

  Microsoft::WRL::ComPtr<IMMDevice> device;
  hr = enumerator->GetDefaultAudioEndpoint(eRender, eConsole,
                                           device.GetAddressOf());
  if (FAILED(hr)) return fail("没有可用的默认播放设备", hr);

  LPWSTR device_id = nullptr;
  if (SUCCEEDED(device->GetId(&device_id)) && device_id != nullptr) {
    impl_->device_id.assign(device_id);
    CoTaskMemFree(device_id);
  } else {
    impl_->device_id.clear();
  }

  hr = device->Activate(
      __uuidof(IAudioClient), CLSCTX_ALL, nullptr,
      reinterpret_cast<void**>(impl_->client.GetAddressOf()));
  if (FAILED(hr)) return fail("系统音采集无法打开播放设备", hr);

  WAVEFORMATEX* mix = nullptr;
  hr = impl_->client->GetMixFormat(&mix);
  if (FAILED(hr) || mix == nullptr) {
    if (mix) CoTaskMemFree(mix);
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
    CoTaskMemFree(mix);
    if (error) {
      *error = "系统音采集不支持当前播放格式（仅支持 float32/PCM16/PCM32）";
    }
    return false;
  }
  impl_->sample_rate = mix->nSamplesPerSec;
  impl_->channels = mix->nChannels;
  if (impl_->channels.load() == 0 || impl_->sample_rate.load() < 8000 ||
      impl_->sample_rate.load() > 192000) {
    CoTaskMemFree(mix);
    if (error) *error = "系统音采集播放格式无效";
    return false;
  }

  hr = impl_->client->Initialize(AUDCLNT_SHAREMODE_SHARED,
                                 AUDCLNT_STREAMFLAGS_LOOPBACK, 0, 0, mix,
                                 nullptr);
  CoTaskMemFree(mix);
  if (FAILED(hr)) return fail("系统音采集无法初始化 loopback", hr);

  hr = impl_->client->GetService(
      IID_PPV_ARGS(impl_->capture.GetAddressOf()));
  if (FAILED(hr)) return fail("系统音采集无法获取 capture 接口", hr);
  return true;
}

void LoopbackAudioCapture::Close() {
  if (!impl_) return;
  impl_->capture.Reset();
  impl_->client.Reset();
}

bool LoopbackAudioCapture::Reopen() {
  if (!impl_) return false;
  if (impl_->client) impl_->client->Stop();
  impl_->capture.Reset();
  impl_->client.Reset();
  std::string error;
  if (!Open(&error)) return false;
  pending_.clear();
  const HRESULT hr = impl_->client->Start();
  if (FAILED(hr)) {
    RecordError(hr);
    return false;
  }
  return true;
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

  int check = 0;
  while (!stop_) {
    if (++check >= kDeviceCheckIterations) {
      check = 0;
      std::wstring current;
      if (DefaultRenderDeviceId(&current) && !current.empty() &&
          current != impl_->device_id && !Reopen()) {
        impl_->device_id.clear();
      }
    }
    if (!impl_->capture || !impl_->client) {
      Sleep(500);
      if (!Reopen()) continue;
    }

    IAudioCaptureClient* capture = impl_->capture.Get();
    UINT32 packet = 0;
    HRESULT hr = capture->GetNextPacketSize(&packet);
    if (FAILED(hr)) {
      RecordError(hr);
      break;
    }
    while (packet > 0 && !stop_) {
      BYTE* data = nullptr;
      UINT32 frame_count = 0;
      DWORD flags = 0;
      hr = capture->GetBuffer(&data, &frame_count, &flags, nullptr, nullptr);
      if (FAILED(hr)) {
        RecordError(hr);
        stop_ = true;
        break;
      }
      if (frame_count > 0) {
        std::vector<std::int16_t> mono;
        if (Convert(data, frame_count, flags, &mono)) {
          Push(mono.data(), mono.size());
        }
      }
      capture->ReleaseBuffer(frame_count);
      hr = capture->GetNextPacketSize(&packet);
      if (FAILED(hr)) {
        RecordError(hr);
        stop_ = true;
        break;
      }
    }
    Sleep(5);
  }

  if (impl_ && impl_->client) impl_->client->Stop();
  running_ = false;
  CoUninitialize();
}

bool LoopbackAudioCapture::Convert(
    const std::uint8_t* data, std::uint32_t frame_count,
    std::uint32_t flags, std::vector<std::int16_t>* out) {
  if (out == nullptr || !impl_ || impl_->channels == 0) return false;
  out->resize(frame_count);
  if ((flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0 || data == nullptr) {
    std::fill(out->begin(), out->end(), 0);
    return true;
  }

  const std::size_t channels = impl_->channels.load();
  switch (impl_->format) {
    case Impl::Format::kFloat32: {
      const auto* src = reinterpret_cast<const float*>(data);
      for (std::uint32_t i = 0; i < frame_count; ++i) {
        double sum = 0;
        for (std::size_t channel = 0; channel < channels; ++channel) {
          sum += src[i * channels + channel];
        }
        const double value = std::clamp(
            sum / static_cast<double>(channels) * 32767.0, -32768.0, 32767.0);
        (*out)[i] = static_cast<std::int16_t>(value);
      }
      break;
    }
    case Impl::Format::kPcm16: {
      const auto* src = reinterpret_cast<const std::int16_t*>(data);
      for (std::uint32_t i = 0; i < frame_count; ++i) {
        std::int32_t sum = 0;
        for (std::size_t channel = 0; channel < channels; ++channel) {
          sum += src[i * channels + channel];
        }
        (*out)[i] =
            static_cast<std::int16_t>(sum / static_cast<std::int32_t>(channels));
      }
      break;
    }
    case Impl::Format::kPcm32: {
      const auto* src = reinterpret_cast<const std::int32_t*>(data);
      for (std::uint32_t i = 0; i < frame_count; ++i) {
        std::int64_t sum = 0;
        for (std::size_t channel = 0; channel < channels; ++channel) {
          sum += src[i * channels + channel];
        }
        (*out)[i] = static_cast<std::int16_t>(
            sum / static_cast<std::int64_t>(channels) >> 16);
      }
      break;
    }
    case Impl::Format::kUnknown:
      return false;
  }
  return true;
}

void LoopbackAudioCapture::Push(const std::int16_t* samples,
                                std::size_t count) {
  if (samples == nullptr || count == 0) return;
  const int source_rate = impl_->sample_rate.load();
  if (source_rate == kTargetSampleRate) {
    pending_.insert(pending_.end(), samples, samples + count);
  } else {
    const double ratio =
        static_cast<double>(kTargetSampleRate) / static_cast<double>(source_rate);
    const std::size_t out_count = static_cast<std::size_t>(count * ratio);
    pending_.reserve(pending_.size() + out_count);
    for (std::size_t i = 0; i < out_count; ++i) {
      const double position = static_cast<double>(i) / ratio;
      const std::size_t index = static_cast<std::size_t>(position);
      const double fraction = position - static_cast<double>(index);
      const std::int16_t first = samples[std::min(index, count - 1)];
      const std::int16_t second = samples[std::min(index + 1, count - 1)];
      pending_.push_back(static_cast<std::int16_t>(
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
  std::size_t offset = 0;
  auto* transport = transport_.load();
  while (pending_.size() - offset >= kSamplesPerFrame) {
    if (transport == nullptr) break;
    std::int16_t frame[kSamplesPerFrame];
    std::copy_n(pending_.begin() + offset, kSamplesPerFrame, frame);
    std::uint32_t level = 0;
    transport->RecordedDataIsAvailable(
        frame, kSamplesPerFrame, kBytesPerSample, kTargetChannels,
        kTargetSampleRate, 0, 0, 0, false, level);
    ++frames_;
    bytes_ += kSamplesPerFrame * kBytesPerSample;
    double energy = 0;
    for (const auto sample : frame) {
      energy += static_cast<double>(sample) * sample;
    }
    rms_ = static_cast<int>(std::sqrt(energy / kSamplesPerFrame));
    offset += kSamplesPerFrame;
  }
  if (offset > 0) {
    pending_.erase(pending_.begin(), pending_.begin() + offset);
  }
}

void LoopbackAudioCapture::RecordError(long hr) { last_error_ = hr; }

}  // namespace rtcwin::webrtc_media
