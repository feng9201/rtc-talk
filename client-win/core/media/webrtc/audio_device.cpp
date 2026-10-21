// File: core/media/webrtc/audio_device.cpp
// Brief: 实现音频设备选择、播放启动与统计。
#include "media/webrtc/audio_device.h"

#include <algorithm>
#include <utility>

#include "api/audio/create_audio_device_module.h"
#include "api/make_ref_counted.h"

namespace talk {
namespace {

constexpr uint16_t kDeviceIndexLimit = 0xffff;

void SetError(std::string* error, const std::string& message) {
  if (error != nullptr) {
    *error = message;
  }
}

}  // namespace

AudioDevice::AudioDevice(webrtc::Thread* worker) : worker_(worker) {}

AudioDevice::~AudioDevice() {
  if (worker_ != nullptr) {
    worker_->BlockingCall([this] {
      if (adm_) {
        adm_->StopRecording();
        adm_->StopPlayout();
        adm_->Terminate();
        adm_ = nullptr;
      }
      switchable_ = nullptr;
      com_.reset();
    });
  } else {
    adm_ = nullptr;
    com_.reset();
  }
  fake_ = nullptr;
}

std::unique_ptr<AudioDevice> AudioDevice::Create(
    const std::string& backend, const std::string& audio_source,
    const webrtc::Environment& env, webrtc::Thread* worker,
    std::string* error) {
  auto device = std::unique_ptr<AudioDevice>(new AudioDevice(worker));
  if (!device->Initialize(backend, audio_source, env, error)) {
    return nullptr;
  }
  return device;
}

bool AudioDevice::Initialize(const std::string& backend,
                             const std::string& audio_source,
                             const webrtc::Environment& env,
                             std::string* error) {
  if (audio_source != "mic" && audio_source != "system") {
    SetError(error, "audio_source 必须是 mic/system");
    return false;
  }
  audio_source_ = audio_source;
  if (backend == "fake") {
    fake_ = webrtc::make_ref_counted<SineAudioDevice>();
    adm_ = fake_;
    return true;
  }
  if (backend != "wasapi" && backend != "default") {
    SetError(error, "未启用音频后端");
    return false;
  }
  if (worker_ == nullptr) {
    SetError(error, "音频线程未启动");
    return false;
  }
  // Core Audio ADM 必须在 COM 线程创建；worker 线程固定为 MTA。
  const bool system_audio = audio_source == "system";
  const bool ready = worker_->BlockingCall([this, &env, system_audio] {
    com_ = std::make_unique<webrtc::ScopedCOMInitializer>(
        webrtc::ScopedCOMInitializer::kMTA);
    if (!com_->Succeeded()) {
      return false;
    }
    auto mic = webrtc::CreateAudioDeviceModule(
        env, webrtc::AudioDeviceModule::kWindowsCoreAudio);
    if (!mic) {
      return false;
    }
    auto switchable = webrtc::make_ref_counted<SwitchableAudioDevice>(
        mic, system_audio);
    if (!switchable) {
      return false;
    }
    switchable_ = switchable;
    adm_ = switchable;
    return adm_->Init() == 0;
  });
  if (!ready) {
    const std::string detail =
        switchable_ ? switchable_->last_error() : std::string();
    SetError(error,
             detail.empty() ? "Windows Core Audio 设备初始化失败" : detail);
    return false;
  }
  return true;
}

Json AudioDevice::Enumerate() const {
  Json result = {{"recording", Json::array()}, {"playout", Json::array()}};
  if (!adm_ || worker_ == nullptr) {
    return result;
  }
  return worker_->BlockingCall([this] {
    Json devices = {{"recording", Json::array()}, {"playout", Json::array()}};
    for (bool recording : {true, false}) {
      const int count =
          recording ? adm_->RecordingDevices() : adm_->PlayoutDevices();
      for (int i = 0; i < count; ++i) {
        char name[webrtc::kAdmMaxDeviceNameSize]{};
        char guid[webrtc::kAdmMaxGuidSize]{};
        const int status = recording ? adm_->RecordingDeviceName(i, name, guid)
                                     : adm_->PlayoutDeviceName(i, name, guid);
        if (status != 0) {
          continue;
        }
        const char* id = guid[0] != '\0' ? guid : name;
        devices[recording ? "recording" : "playout"].push_back(
            {{"index", i}, {"name", name}, {"id", id}});
      }
    }
    return devices;
  });
}

bool AudioDevice::SelectDevices(bool need_recording, const Json& config,
                                std::string* error) {
  if (fake_) {
    recording_selected_ = true;
    playout_selected_ = true;
    return true;
  }
  // 系统音模式不需要选择麦克风；播放设备仍需选择以接收对端声音。
  if (audio_source_ == "system") {
    recording_selected_ = true;
    recording_name_ = "system-audio";
  }
  const Json list = Enumerate();
  for (bool recording : {true, false}) {
    if (recording ? (!need_recording || recording_selected_)
                  : playout_selected_) {
      continue;
    }
    const auto& items = list[recording ? "recording" : "playout"];
    if (items.empty()) {
      SetError(error, recording ? "未检测到采集设备（麦克风）"
                                : "未检测到播放设备");
      return false;
    }
    const std::string key = recording ? "recording_device" : "playout_device";
    std::string wanted;
    if (config.contains(key)) {
      if (!config[key].is_string()) {
        SetError(error, "音频设备配置必须是字符串");
        return false;
      }
      wanted = config[key].get<std::string>();
    }
    int index = -1;
    if (!wanted.empty()) {
      for (const auto& item : items) {
        if (item.is_object() && item.value("id", std::string()) == wanted) {
          index = item.value("index", -1);
          break;
        }
      }
    }
    int status = -1;
    if (index >= 0) {
      status = worker_->BlockingCall([this, recording, index] {
        const auto slot = static_cast<uint16_t>(
            std::min(index, static_cast<int>(kDeviceIndexLimit)));
        const int32_t result = recording ? adm_->SetRecordingDevice(slot)
                                         : adm_->SetPlayoutDevice(slot);
        if (result == 0) {
          char name[webrtc::kAdmMaxDeviceNameSize]{};
          char guid[webrtc::kAdmMaxGuidSize]{};
          if (recording) {
            adm_->RecordingDeviceName(slot, name, guid);
            recording_name_ = name;
          } else {
            adm_->PlayoutDeviceName(slot, name, guid);
            playout_name_ = name;
          }
        }
        return static_cast<int>(result);
      });
    }
    if (status != 0) {
      status = worker_->BlockingCall([this, recording] {
        if (recording) {
          if (adm_->SetRecordingDevice(
                  webrtc::AudioDeviceModule::kDefaultCommunicationDevice) !=
              0) {
            return -1;
          }
          recording_name_ = "default-communication";
        } else {
          if (adm_->SetPlayoutDevice(
                  webrtc::AudioDeviceModule::kDefaultCommunicationDevice) !=
              0) {
            return -1;
          }
          playout_name_ = "default-communication";
        }
        return 0;
      });
    }
    if (status != 0) {
      SetError(error, "音频设备不可用");
      return false;
    }
    (recording ? recording_selected_ : playout_selected_) = true;
  }
  return true;
}

bool AudioDevice::EnsurePlayout() {
  if (fake_ || playout_started_ || worker_ == nullptr) {
    return fake_ || playout_started_;
  }
  // 必须在 AudioTransport 注册之后再启动 playout：Core Audio 在媒体已启动时
  // 会拒绝 RegisterAudioCallback。内置 AEC 也要求 playout 先于 recording。
  playout_started_ = worker_->BlockingCall([this] {
    if (adm_->Playing()) {
      return true;
    }
    if (!adm_->PlayoutIsInitialized() && adm_->InitPlayout() != 0) {
      return false;
    }
    return adm_->StartPlayout() == 0;
  });
  if (!playout_started_) {
    playout_error_ = true;
  }
  return playout_started_;
}

Json AudioDevice::Stats() const {
  Json out = Json::object();
  if (fake_) {
    out["fake_playout"] = fake_->stats();
  }
  out["capture"] = {{"frames", capture_frames_.load()}};
  out["audio_source"] = audio_source_;
  if (switchable_ && worker_ != nullptr) {
    out["switchable"] =
        worker_->BlockingCall([this] { return switchable_->stats(); });
  }
  if (adm_ && !fake_ && worker_ != nullptr) {
    out["device"] = worker_->BlockingCall([this] {
      bool stereo_recording = false;
      bool stereo_playout = false;
      uint16_t delay_ms = 0;
      adm_->StereoRecording(&stereo_recording);
      adm_->StereoPlayout(&stereo_playout);
      adm_->PlayoutDelay(&delay_ms);
      return Json{{"recording", adm_->Recording()},
                  {"playing", adm_->Playing()},
                  {"recording_initialized", adm_->RecordingIsInitialized()},
                  {"playout_initialized", adm_->PlayoutIsInitialized()},
                  {"stereo_recording", stereo_recording},
                  {"stereo_playout", stereo_playout},
                  {"playout_delay_ms", delay_ms},
                  {"playout_error", playout_error_},
                  {"recording_device", recording_name_},
                  {"playout_device", playout_name_}};
    });
  }
  return out;
}

}  // namespace talk
