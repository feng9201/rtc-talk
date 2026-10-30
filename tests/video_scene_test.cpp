#include "rtcwin/application/video_scene.h"
#include "rtcwin/ui/video/video_layout.h"

#include <iostream>

namespace {

int failures = 0;

void Expect(bool condition, const char* message) {
  if (condition) return;
  ++failures;
  std::cerr << "FAIL: " << message << '\n';
}

}  // namespace

int main() {
  rtcwin::domain::SessionSnapshot state;
  state.self_uid = "u-self";
  state.self_display_name = "Self";
  state.camera_on = true;
  state.sharing = true;
  state.selected_uid = state.self_uid;
  rtcwin::domain::PeerState peer;
  peer.uid = "u-peer";
  peer.display_name = "Peer";
  peer.camera_on = true;
  state.peers.push_back(peer);

  auto scene = rtcwin::application::VideoSceneBuilder::Build(
      state, rtcwin::domain::StageLayout::Grid,
      rtcwin::domain::StageFilter::All);
  Expect(scene.mode == rtcwin::application::VideoSceneMode::Shared,
         "screen share did not select shared layout");
  Expect(scene.primary.size() == 1, "shared scene must have one main tile");
  Expect(scene.primary.front().source ==
             rtcwin::domain::VideoSource::Screen,
         "shared main tile is not a screen");
  Expect(scene.primary.front().has_camera_pip,
         "screen tile has no camera PIP");
  Expect(scene.secondary.size() == 1, "peer tile was dropped");

  state.sharing = false;
  state.peers.front().camera_on = false;
  scene = rtcwin::application::VideoSceneBuilder::Build(
      state, rtcwin::domain::StageLayout::Focus,
      rtcwin::domain::StageFilter::VideoOnly);
  Expect(scene.mode == rtcwin::application::VideoSceneMode::Focus,
         "camera-only focus layout is incorrect");
  Expect(scene.primary.front().source ==
             rtcwin::domain::VideoSource::Camera,
         "camera-only focus tile is not camera");
  Expect(scene.secondary.empty(), "video-only filter kept an empty peer");

  state.sharing = false;
  state.peers.front().camera_on = true;
  state.selected_uid = "u-peer";
  scene = rtcwin::application::VideoSceneBuilder::Build(
      state, rtcwin::domain::StageLayout::Grid,
      rtcwin::domain::StageFilter::All);
  Expect(scene.mode == rtcwin::application::VideoSceneMode::Grid,
         "camera-only grid did not use grid mode");
  Expect(scene.primary.size() == 2,
         "camera-only grid must expose one tile per active stream");
  auto layout = rtcwin::ui::VideoLayoutEngine::Build(scene);
  Expect(layout.columns == 2 && layout.rows == 1,
         "two-stream grid layout dimensions are incorrect");

  state.sharing = true;
  state.selected_uid = "u-peer";
  state.peers.front().camera_on = true;
  scene = rtcwin::application::VideoSceneBuilder::Build(
      state, rtcwin::domain::StageLayout::Focus,
      rtcwin::domain::StageFilter::All);
  Expect(scene.mode == rtcwin::application::VideoSceneMode::Shared,
         "screen sharing did not override focus with shared mode");
  Expect(scene.primary.front().source ==
             rtcwin::domain::VideoSource::Screen,
         "shared focus request did not prefer the screen stream");
  Expect(scene.primary.front().has_camera_pip,
         "shared focus request lost the camera PIP");
  layout = rtcwin::ui::VideoLayoutEngine::Build(scene);
  Expect(layout.columns == 6 && layout.rows == 4 &&
             layout.cells.size() == 2,
         "shared focus PIP layout dimensions are incorrect");

  state.selected_uid = state.self_uid;
  scene = rtcwin::application::VideoSceneBuilder::Build(
      state, rtcwin::domain::StageLayout::Grid,
      rtcwin::domain::StageFilter::All);
  layout = rtcwin::ui::VideoLayoutEngine::Build(scene);
  Expect(layout.columns == 6 && layout.rows == 4,
         "shared layout dimensions are incorrect");
  Expect(layout.cells.size() == 2,
         "shared layout must include primary and secondary tiles");
  Expect(layout.cells.front().row_span == 3 &&
             layout.cells.front().column_span == 6,
         "shared primary does not span the main area");

  state.sharing = false;
  scene = rtcwin::application::VideoSceneBuilder::Build(
      state, rtcwin::domain::StageLayout::Focus,
      rtcwin::domain::StageFilter::All);
  layout = rtcwin::ui::VideoLayoutEngine::Build(scene);
  Expect(layout.columns == 3 && layout.rows == 3,
         "focus layout dimensions are incorrect");
  Expect(layout.cells.front().column_span == 2,
         "focus primary does not use the main area");

  if (failures != 0) {
    std::cerr << failures << " video scene test(s) failed\n";
    return 1;
  }
  std::cout << "video_scene_test: PASS\n";
  return 0;
}
