#include "audio_pipeline.h"

#include "media_error.h"
#include "rtcwin/webrtc_media/switchable_audio_device.h"

#include "api/audio/audio_frame.h"
#include "api/audio/audio_frame_processor.h"
#include "rtc_base/logging.h"
#include "rtc_base/win/scoped_com_initializer.h"

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <mutex>
#include <utility>

namespace rtcwin::webrtc_media {
namespace {

class CaptureMonitor final : public webrtc::AudioTrackSinkInterface {
 public:
  void OnData(const void*, int, int sample_rate, size_t channels,
              size_t frames) override {
    (void)sample_rate;
    (void)channels;
    frames_.fetch_add(frames, std::memory_order_relaxed);
  }

  std::uint64_t frames() const {
    return frames_.load(std::memory_order_relaxed);
  }

 private:
  std::atomic<std::uint64_t> frames_{0};
};

class CaptureGate final : public webrtc::AudioFrameProcessor {
 public:
  void Process(std::unique_ptr<webrtc::AudioFrame> frame) override {
    if (!frame) return;
    capture_frames_.fetch_add(1, std::memory_order_relaxed);
    if (muted_.load(std::memory_order_relaxed)) frame->Mute();
    OnAudioFrameCallback sink;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      sink = sink_;
    }
    if (sink) sink(std::move(frame));
  }

  void SetSink(OnAudioFrameCallback sink) override {
    std::lock_guard<std::mutex> lock(mutex_);
    sink_ = std::move(sink);
  }

  void SetMuted(bool muted) { muted_.store(muted, std::memory_order_relaxed); }
  std::uint64_t frames() const {
    return capture_frames_.load(std::memory_order_relaxed);
  }

 private:
  std::atomic<bool> muted_{true};
  std::atomic<std::uint64_t> capture_frames_{0};
  std::mutex mutex_;
  OnAudioFrameCallback sink_;
};

bool SelectDevice(webrtc::AudioDeviceModule* adm, bool recording,
                  const std::string& wanted, std::string* selected,
                  std::string* error) {
  if (adm == nullptr) {
    if (error) *error = "音频设备模块为空";
    return false;
  }
  int found_index = -1;
  int fallback_index = -1;
  const int count = recording ? adm->RecordingDevices() : adm->PlayoutDevices();
  for (int index = 0; index < count; ++index) {
    char name[webrtc::kAdmMaxDeviceNameSize]{};
    char guid[webrtc::kAdmMaxGuidSize]{};
    const int status = recording ? adm->RecordingDeviceName(index, name, guid)
                                 : adm->PlayoutDeviceName(index, name, guid);
    if (status != 0) continue;
    if (fallback_index < 0) fallback_index = index;
    if (!wanted.empty() && (wanted == guid || wanted == name)) {
      found_index = index;
      break;
    }
  }
  if (found_index < 0) found_index = fallback_index;
  if (found_index < 0) {
    if (error) {
      *error = recording ? "未检测到麦克风设备" : "未检测到播放设备";
    }
    return false;
  }
  const auto slot = static_cast<uint16_t>(std::min(found_index, 65535));
  int status = recording ? adm->SetRecordingDevice(slot)
                         : adm->SetPlayoutDevice(slot);
  if (status != 0) {
    status = recording
                 ? adm->SetRecordingDevice(
                       webrtc::AudioDeviceModule::kDefaultCommunicationDevice)
                 : adm->SetPlayoutDevice(
                       webrtc::AudioDeviceModule::kDefaultCommunicationDevice);
    if (status != 0) {
      if (error) *error = recording ? "选择麦克风失败" : "选择播放设备失败";
      return false;
    }
  }
  char name[webrtc::kAdmMaxDeviceNameSize]{};
  char guid[webrtc::kAdmMaxGuidSize]{};
  const int name_status =
      recording ? adm->RecordingDeviceName(slot, name, guid)
                : adm->PlayoutDeviceName(slot, name, guid);
  if (selected) {
    *selected = name_status == 0 && name[0] != '\0' ? name : "default";
  }
  return true;
}

}  // namespace

class CaptureManager final {
 public:
  CaptureManager() = default;
  ~CaptureManager() { capture_monitor.reset(); }

  std::unique_ptr<webrtc::AudioFrameProcessor> CreateGate(bool muted) {
    auto gate = std::make_unique<CaptureGate>();
    gate->SetMuted(muted);
    auto* raw = gate.get();
    gate_ = raw;
    return gate;
  }

  void SetGate(CaptureGate* gate) { gate_ = gate; }
  CaptureGate* gate() const { return gate_; }
  void SetMuted(bool muted) {
    if (gate_) gate_->SetMuted(muted);
  }
  std::uint64_t gate_frames() const { return gate_ ? gate_->frames() : 0; }

  void AttachSink(webrtc::scoped_refptr<webrtc::AudioTrackInterface> track) {
    if (!track) return;
    capture_monitor = std::make_unique<CaptureMonitor>();
    if (auto* source = track->GetSource()) {
      source->AddSink(capture_monitor.get());
    }
  }
  void DetachSink(webrtc::scoped_refptr<webrtc::AudioTrackInterface> track) {
    if (track && capture_monitor) {
      if (auto* source = track->GetSource()) {
        source->RemoveSink(capture_monitor.get());
      }
    }
    capture_monitor.reset();
  }
  std::uint64_t monitor_frames() const {
    return capture_monitor ? capture_monitor->frames() : 0;
  }

 private:
  std::unique_ptr<CaptureMonitor> capture_monitor;
  CaptureGate* gate_ = nullptr;
};

AudioPipeline::AudioPipeline() = default;
AudioPipeline::~AudioPipeline() = default;

bool AudioPipeline::Initialize(webrtc::Thread* worker,
                               const webrtc::Environment& environment,
                               const MediaConfig& config,
                               MediaError* error) {
  worker_ = worker;
  fake_device_ = config.audio_backend == "fake";
  volume_.store(std::clamp(config.volume, 0, 100) / 100.0,
                std::memory_order_relaxed);
  muted_.store(config.muted, std::memory_order_relaxed);
  if (!InitializeDevices(worker, environment, config, error)) return false;
  capture_manager_ = std::make_unique<CaptureManager>();
  initialized_ = true;
  return true;
}

std::unique_ptr<webrtc::AudioFrameProcessor>
AudioPipeline::CreateFrameProcessor() {
  if (!capture_manager_) capture_manager_ = std::make_unique<CaptureManager>();
  auto gate = capture_manager_->CreateGate(
      muted_.load(std::memory_order_relaxed));
  capture_gate_ = gate.get();
  return gate;
}

bool AudioPipeline::CreateTrack(
    webrtc::Thread* signaling,
    webrtc::PeerConnectionFactoryInterface* factory,
    const MediaConfig& config, MediaError* error) {
  if (!factory || !signaling) {
    SetError(error, 1707, "创建本地音轨失败");
    return false;
  }
  signaling_ = signaling;
  const bool ok = signaling->BlockingCall([this, factory, &config] {
    auto source = factory->CreateAudioSource(webrtc::AudioOptions());
    if (!source) return false;
    audio_track_ = factory->CreateAudioTrack(
        config.audio_source == "system" ? "rtcwin-system" : "rtcwin-mic",
        source.get());
    if (!audio_track_) return false;
    audio_track_->set_enabled(true);
    capture_manager_->AttachSink(audio_track_);
    return true;
  });
  if (!ok) {
    SetError(error, 1707, "创建本地音轨失败");
    return false;
  }
  return true;
}

void AudioPipeline::Shutdown(webrtc::Thread* worker) {
  if (signaling_ && audio_track_) {
    signaling_->BlockingCall([this] {
      capture_manager_->DetachSink(audio_track_);
      audio_track_->set_enabled(false);
    });
  }
  audio_track_ = nullptr;
  capture_manager_.reset();
  capture_gate_ = nullptr;
  if (worker && adm_) {
    worker->BlockingCall([this] {
      if (adm_->Playing()) adm_->StopPlayout();
      if (adm_->Recording()) adm_->StopRecording();
      adm_->Terminate();
      adm_ = nullptr;
      switchable_ = nullptr;
      com_.reset();
    });
  } else {
    adm_ = nullptr;
    switchable_ = nullptr;
    com_.reset();
  }
  worker_ = nullptr;
  signaling_ = nullptr;
  initialized_ = false;
}

bool AudioPipeline::InitializeDevices(
    webrtc::Thread* worker, const webrtc::Environment& environment,
    const MediaConfig& config, MediaError* error) {
  if (!worker) {
    SetError(error, 1705, "音频工作线程未初始化");
    return false;
  }
  const auto layer = config.audio_backend == "fake"
                         ? webrtc::AudioDeviceModule::kDummyAudio
                         : webrtc::AudioDeviceModule::kWindowsCoreAudio;
  const bool system_audio = config.audio_source == "system";
  std::string device_error;
  const bool ok = worker->BlockingCall(
      [this, &environment, layer, system_audio, &config, &device_error] {
        if (layer == webrtc::AudioDeviceModule::kWindowsCoreAudio) {
          com_ = std::make_unique<webrtc::ScopedCOMInitializer>(
              webrtc::ScopedCOMInitializer::kMTA);
          if (!com_->Succeeded()) {
            device_error = "COM MTA 初始化失败";
            return false;
          }
        }
        auto core_adm = webrtc::CreateAudioDeviceModule(environment, layer);
        if (!core_adm) {
          device_error = "音频设备模块初始化失败";
          return false;
        }
        if (layer == webrtc::AudioDeviceModule::kWindowsCoreAudio) {
          switchable_ = webrtc::make_ref_counted<SwitchableAudioDevice>(
              core_adm, system_audio);
          adm_ = switchable_;
        } else {
          adm_ = core_adm;
        }
        if (adm_->Init() != 0) {
          device_error = "音频设备模块初始化失败";
          return false;
        }
        if (layer == webrtc::AudioDeviceModule::kDummyAudio) {
          recording_name_ = "fake-microphone";
          playout_name_ = "fake-speaker";
          return true;
        }
        if (system_audio) {
          recording_name_ = "system-default-render";
        } else if (!SelectDevice(adm_.get(), true, config.recording_device,
                                 &recording_name_, &device_error)) {
          return false;
        }
        return SelectDevice(adm_.get(), false, config.playout_device,
                            &playout_name_, &device_error);
      });
  if (!ok) SetError(error, 1705, device_error);
  return ok;
}

bool AudioPipeline::EnsurePlayout(std::string* detail) {
  if (!adm_ || !worker_) {
    if (detail) *detail = "ADM 未初始化";
    return false;
  }
  if (fake_device_) return true;
  const bool ok = worker_->BlockingCall([this] {
    if (!adm_->PlayoutIsInitialized() && adm_->InitPlayout() != 0) return false;
    return adm_->Playing() || adm_->StartPlayout() == 0;
  });
  if (!ok && detail) *detail = "启动音频播放失败";
  return ok;
}

bool AudioPipeline::SetMuted(bool muted, MediaError* error) {
  if (!initialized_) {
    SetError(error, 1720, "音频媒体会话未初始化");
    return false;
  }
  muted_.store(muted, std::memory_order_relaxed);
  if (capture_manager_) capture_manager_->SetMuted(muted);
  if (audio_track_ && signaling_) {
    signaling_->BlockingCall([this, muted] {
      audio_track_->set_enabled(!muted);
    });
  }
  return true;
}

bool AudioPipeline::SetDeafened(bool deafened) {
  deafened_.store(deafened, std::memory_order_relaxed);
  return true;
}

bool AudioPipeline::SetVolume(double volume, MediaError* error) {
  volume_.store(volume, std::memory_order_relaxed);
  return true;
}

std::uint64_t AudioPipeline::capture_frames() const {
  return std::max(capture_manager_ ? capture_manager_->gate_frames() : 0,
                  capture_manager_ ? capture_manager_->monitor_frames() : 0);
}

void AudioPipeline::FillStatus(webrtc::Thread* worker,
                               MediaStatus* status) const {
  if (!status) return;
  status->muted = muted_.load(std::memory_order_relaxed);
  status->deafened = deafened_.load(std::memory_order_relaxed);
  status->volume = volume_.load(std::memory_order_relaxed);
  status->capture_frames = capture_frames();
  if (adm_ && worker) {
    worker->BlockingCall([this, status] {
      status->recording_initialized = adm_->RecordingIsInitialized();
      status->recording = adm_->Recording();
      status->playout_initialized = adm_->PlayoutIsInitialized();
      status->playing = adm_->Playing();
    });
  }
  status->recording_device = recording_name_;
  status->playout_device = playout_name_;
}

}  // namespace rtcwin::webrtc_media
