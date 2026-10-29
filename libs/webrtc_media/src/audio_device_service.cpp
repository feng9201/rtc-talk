#include "rtcwin/webrtc_media/audio_device_service.h"

#include "api/audio/create_audio_device_module.h"
#include "api/environment/environment_factory.h"
#include "rtc_base/thread.h"
#include "rtc_base/win/scoped_com_initializer.h"

#include <memory>
#include <utility>

namespace rtcwin::webrtc_media {
namespace {

void SetError(std::string* error, const std::string& message) {
  if (error != nullptr) *error = message;
}

std::vector<AudioDeviceInfo> CollectDevices(
    webrtc::AudioDeviceModule* adm, bool recording) {
  std::vector<AudioDeviceInfo> devices;
  const int count =
      recording ? adm->RecordingDevices() : adm->PlayoutDevices();
  for (int index = 0; index < count; ++index) {
    char name[webrtc::kAdmMaxDeviceNameSize]{};
    char guid[webrtc::kAdmMaxGuidSize]{};
    const int status = recording ? adm->RecordingDeviceName(index, name, guid)
                                 : adm->PlayoutDeviceName(index, name, guid);
    if (status != 0) continue;
    AudioDeviceInfo device;
    device.index = index;
    device.name = name;
    device.id = guid[0] != '\0' ? guid : name;
    devices.push_back(std::move(device));
  }
  return devices;
}

}  // namespace

bool EnumerateAudioDevices(AudioDeviceList* devices, std::string* error) {
  if (devices == nullptr) {
    SetError(error, "设备输出不能为空");
    return false;
  }
  *devices = {};

  auto worker = webrtc::Thread::Create();
  if (!worker) {
    SetError(error, "音频线程创建失败");
    return false;
  }
  worker->SetName("rtcwin-audio-devices", nullptr);
  if (!worker->Start()) {
    SetError(error, "音频线程启动失败");
    return false;
  }

  std::string worker_error;
  const bool ok = worker->BlockingCall([devices, &worker_error] {
    webrtc::ScopedCOMInitializer com(
        webrtc::ScopedCOMInitializer::kMTA);
    if (!com.Succeeded()) {
      worker_error = "COM MTA 初始化失败";
      return false;
    }
    const webrtc::Environment env = webrtc::CreateEnvironment();
    auto adm = webrtc::CreateAudioDeviceModule(
        env, webrtc::AudioDeviceModule::kWindowsCoreAudio);
    if (!adm) {
      worker_error = "Windows Core Audio ADM 创建失败";
      return false;
    }
    if (adm->Init() != 0) {
      worker_error = "Windows Core Audio ADM 初始化失败";
      return false;
    }
    devices->recording = CollectDevices(adm.get(), true);
    devices->playout = CollectDevices(adm.get(), false);
    adm->Terminate();
    return true;
  });
  worker->Stop();
  if (!ok) {
    SetError(error, worker_error.empty() ? "音频设备枚举失败" : worker_error);
    return false;
  }
  return true;
}

}  // namespace rtcwin::webrtc_media