#include "rtcwin/webrtc_media/audio_device_service.h"

#include <iostream>
#include <string>

int main() {
  rtcwin::webrtc_media::AudioDeviceList devices;
  std::string error;
  if (!rtcwin::webrtc_media::EnumerateAudioDevices(&devices, &error)) {
    std::cerr << "audio device enumeration failed: " << error << '\n';
    return 1;
  }
  std::cout << "recording devices=" << devices.recording.size() << '\n';
  for (const auto& device : devices.recording) {
    std::cout << "  [" << device.index << "] " << device.name << '\n';
  }
  std::cout << "playout devices=" << devices.playout.size() << '\n';
  for (const auto& device : devices.playout) {
    std::cout << "  [" << device.index << "] " << device.name << '\n';
  }
  return 0;
}