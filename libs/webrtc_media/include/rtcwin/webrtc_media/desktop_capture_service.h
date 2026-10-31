#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace rtcwin::webrtc_media {

enum class DesktopSourceType {
  Screen,
  Window,
};

struct DesktopSourceInfo {
  std::int64_t id = 0;
  std::string title;
  DesktopSourceType type = DesktopSourceType::Screen;
};

bool EnumerateDesktopSources(std::vector<DesktopSourceInfo>* sources,
                             std::string* error);

}  // namespace rtcwin::webrtc_media
