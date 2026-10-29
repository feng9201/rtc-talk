#pragma once

#include <string>
#include <vector>

namespace rtcwin::webrtc_media {

struct AudioDeviceInfo {
  int index = -1;
  std::string id;
  std::string name;
};

struct AudioDeviceList {
  std::vector<AudioDeviceInfo> recording;
  std::vector<AudioDeviceInfo> playout;
};

// Enumerates Windows Core Audio devices through libwebrtc's ADM.
bool EnumerateAudioDevices(AudioDeviceList* devices, std::string* error);

}  // namespace rtcwin::webrtc_media