#include "rtcwin/ui/video/video_layout.h"

#include <algorithm>

namespace rtcwin::ui {
namespace {

int GridColumns(int item_count) {
  if (item_count <= 1) return 1;
  if (item_count <= 4) return 2;
  return 3;
}

}  // namespace

VideoLayoutPlan VideoLayoutEngine::Build(
    const application::VideoScene& scene) {
  VideoLayoutPlan plan;

  if (scene.mode == application::VideoSceneMode::Shared &&
      !scene.primary.empty()) {
    const int secondary_count = static_cast<int>(scene.secondary.size());
    if (secondary_count == 0) {
      plan.rows = 1;
      plan.columns = 1;
      const auto& item = scene.primary.front();
      plan.cells.push_back(VideoLayoutCell{
          application::VideoSceneKey(item), 0, 0, 1, 1, true});
      return plan;
    }

    // P5 shared layout: screen as a full-width main area with a bottom
    // participant strip. Keeping the strip to one-sixth columns preserves
    // readable camera tiles while leaving unused space when the room is small.
    plan.rows = 4;
    plan.columns = std::max(6, secondary_count);
    const auto& item = scene.primary.front();
    plan.cells.push_back(VideoLayoutCell{
        application::VideoSceneKey(item), 0, 0, 3, plan.columns, true});
    for (int index = 0; index < secondary_count; ++index) {
      plan.cells.push_back(VideoLayoutCell{
          application::VideoSceneKey(scene.secondary.at(index)),
          3,
          index,
          1,
          1,
          false});
    }
    return plan;
  }

  if (scene.mode == application::VideoSceneMode::Focus &&
      !scene.primary.empty()) {
    plan.rows = std::max(3, static_cast<int>(scene.secondary.size()));
    plan.columns = scene.secondary.empty() ? 1 : 3;
    if (scene.secondary.empty()) {
      const auto& item = scene.primary.front();
      plan.cells.push_back(VideoLayoutCell{
          application::VideoSceneKey(item), 0, 0, plan.rows, 1, true});
      return plan;
    }

    const auto& item = scene.primary.front();
    plan.cells.push_back(VideoLayoutCell{
        application::VideoSceneKey(item), 0, 0, plan.rows, 2, true});
    for (int index = 0;
         index < static_cast<int>(scene.secondary.size()); ++index) {
      plan.cells.push_back(VideoLayoutCell{
          application::VideoSceneKey(scene.secondary.at(index)),
          index,
          2,
          1,
          1,
          false});
    }
    return plan;
  }

  plan.columns = GridColumns(static_cast<int>(scene.primary.size()));
  plan.rows = plan.columns == 0
                  ? 0
                  : (static_cast<int>(scene.primary.size()) +
                     plan.columns - 1) /
                        plan.columns;
  for (int index = 0;
       index < static_cast<int>(scene.primary.size()); ++index) {
    plan.cells.push_back(VideoLayoutCell{
        application::VideoSceneKey(scene.primary.at(index)),
        index / plan.columns,
        index % plan.columns,
        1,
        1,
        true});
  }
  return plan;
}

}  // namespace rtcwin::ui
