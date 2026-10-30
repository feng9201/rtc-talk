#pragma once

#include "rtcwin/domain/types.h"

#include <string>
#include <vector>

namespace rtcwin::application {

enum class VideoSceneMode {
  Grid,
  Focus,
  Shared,
};

struct VideoSceneItem {
  std::string uid;
  std::string display_name;
  domain::VideoSource source = domain::VideoSource::Camera;
  bool self = false;
  bool selected = false;
  bool has_camera_pip = false;
};

struct VideoScene {
  VideoSceneMode mode = VideoSceneMode::Grid;
  std::vector<VideoSceneItem> primary;
  std::vector<VideoSceneItem> secondary;
};

class VideoSceneBuilder final {
 public:
  static VideoScene Build(const domain::SessionSnapshot& snapshot,
                          domain::StageLayout layout,
                          domain::StageFilter filter);
};

std::string VideoSceneKey(const VideoSceneItem& item);

}  // namespace rtcwin::application
