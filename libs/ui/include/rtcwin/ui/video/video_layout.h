#pragma once

#include "rtcwin/application/video_scene.h"

#include <string>
#include <vector>

namespace rtcwin::ui {

struct VideoLayoutCell {
  std::string key;
  int row = 0;
  int column = 0;
  int row_span = 1;
  int column_span = 1;
  bool primary = false;
};

struct VideoLayoutPlan {
  int rows = 0;
  int columns = 0;
  std::vector<VideoLayoutCell> cells;
};

// Converts a presentation scene into a deterministic grid policy. The engine
// deliberately has no QWidget dependency so layout behavior can be tested
// without a window or OpenGL context.
class VideoLayoutEngine final {
 public:
  static VideoLayoutPlan Build(const application::VideoScene& scene);
};

}  // namespace rtcwin::ui
