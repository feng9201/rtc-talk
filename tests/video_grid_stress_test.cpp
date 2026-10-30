#include "rtcwin/muxplayer_adapter/mux_video_surface.h"
#include "rtcwin/ui/video/video_grid.h"

#include <QApplication>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QSet>
#include <QString>

#include <cstdint>
#include <iostream>
#include <string>

namespace {

int failures = 0;

void Expect(bool condition, const char* message) {
  if (condition) return;
  ++failures;
  std::cerr << "FAIL: " << message << '\n';
}

QSet<QString> SurfaceKeys(const rtcwin::ui::VideoGrid& grid) {
  QSet<QString> keys;
  const auto surfaces =
      grid.findChildren<rtcwin::muxplayer_adapter::MuxVideoSurface*>();
  for (const auto* surface : surfaces) {
    const QString key = surface->property("streamKey").toString();
    if (!key.isEmpty()) keys.insert(key);
  }
  return keys;
}

rtcwin::domain::VideoFrame MakeFrame(const std::string& uid,
                                    std::uint8_t seed) {
  rtcwin::domain::VideoFrame frame;
  frame.uid = uid;
  frame.mid = uid;
  frame.width = 1280;
  frame.height = 720;
  frame.stride_y = frame.width;
  frame.stride_u = frame.width / 2;
  frame.stride_v = frame.width / 2;
  frame.y.assign(static_cast<std::size_t>(frame.stride_y) * frame.height,
                 16 + seed);
  frame.u.assign(static_cast<std::size_t>(frame.stride_u) *
                     ((frame.height + 1) / 2),
                 128);
  frame.v.assign(static_cast<std::size_t>(frame.stride_v) *
                     ((frame.height + 1) / 2),
                 128);
  return frame;
}

}  // namespace

int main(int argc, char** argv) {
  QApplication app(argc, argv);
  rtcwin::domain::SessionSnapshot state;
  state.connection = rtcwin::domain::ConnectionPhase::Ready;
  state.join = rtcwin::domain::JoinPhase::Joined;
  state.self_uid = "u0";
  state.self_display_name = "User 0";
  state.camera_on = true;
  for (int index = 1; index < 9; ++index) {
    rtcwin::domain::PeerState peer;
    peer.uid = "u" + std::to_string(index);
    peer.display_name = "User " + std::to_string(index);
    peer.camera_on = true;
    state.peers.push_back(peer);
  }

  rtcwin::ui::VideoGrid grid;
  grid.resize(1440, 900);
  grid.SetState(state);
  QCoreApplication::processEvents();

  auto diagnostics = grid.Diagnostics();
  Expect(diagnostics.tiles == 9, "nine-video scene did not create nine tiles");
  Expect(diagnostics.active_surfaces == 9,
         "nine-video scene did not acquire nine surfaces");
  const QSet<QString> keys = SurfaceKeys(grid);
  Expect(keys.size() == 9, "surface stream keys are duplicated or missing");

  QElapsedTimer timer;
  timer.start();
  for (int index = 0; index < 9; ++index) {
    grid.SetFrame(MakeFrame("u" + std::to_string(index), index));
  }
  QCoreApplication::processEvents();
  const qint64 elapsed_ms = timer.elapsed();
  diagnostics = grid.Diagnostics();
  Expect(diagnostics.cached_frames == 9,
         "nine 720p frames were not cached independently");
  Expect(elapsed_ms < 2000,
         "nine 720p frame intake exceeded the short-test budget");
  std::cout << "nine_streams_720p_intake_ms=" << elapsed_ms << '\n';

  state.peers.front().camera_on = false;
  grid.SetState(state);
  QCoreApplication::processEvents();
  diagnostics = grid.Diagnostics();
  Expect(diagnostics.tiles == 9,
         "disabled stream should remain as a placeholder tile");
  Expect(diagnostics.active_surfaces == 8 && diagnostics.idle_surfaces == 1,
         "disabled stream did not return its surface to the pool");

  state.peers.front().camera_on = true;
  grid.SetState(state);
  QCoreApplication::processEvents();
  diagnostics = grid.Diagnostics();
  Expect(diagnostics.tiles == 9 && diagnostics.active_surfaces == 9 &&
             diagnostics.idle_surfaces == 0,
         "re-enabled stream did not reuse the pooled surface");
  Expect(SurfaceKeys(grid).size() == 9,
         "re-enabled scene has duplicate surface keys");

  for (int iteration = 0; iteration < 100; ++iteration) {
    state.peers.front().camera_on = (iteration % 2) != 0;
    grid.SetState(state);
    grid.SetLayoutMode(iteration % 2 == 0
                           ? rtcwin::domain::StageLayout::Grid
                           : rtcwin::domain::StageLayout::Focus);
  }
  state.peers.front().camera_on = true;
  grid.SetState(state);
  grid.SetLayoutMode(rtcwin::domain::StageLayout::Grid);
  diagnostics = grid.Diagnostics();
  Expect(diagnostics.active_surfaces + diagnostics.idle_surfaces <= 9,
         "surface count grew across 100 layout/visibility cycles");

  rtcwin::domain::SessionSnapshot shared_state;
  shared_state.connection = rtcwin::domain::ConnectionPhase::Ready;
  shared_state.join = rtcwin::domain::JoinPhase::Joined;
  shared_state.self_uid = "u0";
  shared_state.self_display_name = "User 0";
  shared_state.camera_on = true;
  shared_state.sharing = true;
  grid.SetState(shared_state);
  grid.SetFrame(MakeFrame("u0", 1));
  auto screen_frame = MakeFrame("u0", 2);
  screen_frame.source = rtcwin::domain::VideoSource::Screen;
  grid.SetFrame(screen_frame);
  Expect(grid.Diagnostics().cached_frames == 2,
         "shared screen and camera PIP were not cached independently");
  grid.SetState(shared_state);
  QCoreApplication::processEvents();
  Expect(grid.Diagnostics().cached_frames == 2,
         "shared PIP camera frame was pruned during scene rebuild");

  if (failures != 0) {
    std::cerr << failures << " video grid stress test(s) failed\n";
    return 1;
  }
  std::cout << "video_grid_stress_test: PASS\n";
  return 0;
}
