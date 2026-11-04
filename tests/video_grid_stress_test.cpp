#include "rtcwin/muxplayer_adapter/mux_video_surface.h"
#include "rtcwin/ui/video/video_grid.h"

#include <QApplication>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QOpenGLWidget>
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

QString SurfaceFrameUid(const rtcwin::ui::VideoGrid& grid,
                        const QString& stream_key) {
  const auto surfaces =
      grid.findChildren<rtcwin::muxplayer_adapter::MuxVideoSurface*>();
  for (const auto* surface : surfaces) {
    if (surface->has_frame() &&
        surface->property("streamKey").toString() == stream_key) {
      return surface->property("frameUid").toString();
    }
  }
  return {};
}

bool HasRenderedFrame(rtcwin::ui::VideoGrid& grid, const QString& stream_key) {
  for (auto* surface :
       grid.findChildren<rtcwin::muxplayer_adapter::MuxVideoSurface*>()) {
    if (surface->property("streamKey").toString() != stream_key) continue;
    auto* gl = surface->findChild<QOpenGLWidget*>();
    if (gl == nullptr || !gl->isVisible() || !gl->isValid()) continue;
    const auto image = gl->grabFramebuffer();
    return !image.isNull() && image.pixelColor(image.rect().center()).red() > 120;
  }
  return false;
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
  const int initial_attach_changes =
      grid.Diagnostics().surface_attach_changes;

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
  Expect(SurfaceFrameUid(grid, QStringLiteral("u0|camera")) ==
             QStringLiteral("u0"),
         "local camera frame was applied to another member's surface");
  Expect(SurfaceFrameUid(grid, QStringLiteral("u8|camera")) ==
             QStringLiteral("u8"),
         "remote camera frame was applied to the wrong member surface");
  Expect(elapsed_ms < 2000,
         "nine 720p frame intake exceeded the short-test budget");
  std::cout << "nine_streams_720p_intake_ms=" << elapsed_ms << '\n';

  for (int iteration = 0; iteration < 20; ++iteration) {
    state.self_audio_level = 0.1 * iteration;
    grid.SetState(state);
  }
  QCoreApplication::processEvents();
  diagnostics = grid.Diagnostics();
  Expect(diagnostics.surface_attach_changes == initial_attach_changes,
         "unchanged scene repeatedly rebound video surfaces");

  state.peers.front().camera_on = false;
  grid.SetState(state);
  QCoreApplication::processEvents();
  diagnostics = grid.Diagnostics();
  Expect(diagnostics.tiles == 9,
         "disabled stream should remain as a placeholder tile");
  Expect(diagnostics.active_surfaces == 8 && diagnostics.idle_surfaces == 1,
         "disabled stream did not return its surface to the pool");
  for (const auto* tile : grid.findChildren<rtcwin::ui::VideoTile*>()) {
    const auto* surface = tile->mainSurface();
    Expect(surface == nullptr || surface->parentWidget() == tile,
           "tile retained a pointer to a released video surface");
  }

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

  rtcwin::domain::PeerState remote;
  remote.uid = "A";
  remote.camera_on = true;
  shared_state.camera_on = false;
  shared_state.media_mode = "p2p";
  shared_state.peers = {remote};
  grid.SetState(shared_state);
  grid.SetFrame(MakeFrame("A", 3));
  shared_state.media_mode = "sfu";
  shared_state.peers.emplace_back().uid = "C";
  // During SFU publication the camera owner may briefly announce a screen
  // track before its paused state arrives.
  shared_state.peers.front().screen_on = true;
  grid.SetState(shared_state);
  shared_state.peers.front().screen_on = false;
  grid.SetState(shared_state);
  const bool check_pixels =
      app.arguments().contains(QStringLiteral("--render-check"));
  if (check_pixels) {
    grid.show();
    QCoreApplication::processEvents();
  }
  auto switched_frame = MakeFrame("A", 184);
  switched_frame.mid = "1";
  grid.SetFrame(switched_frame);
  Expect(SurfaceFrameUid(grid, QStringLiteral("A|camera")) ==
             QStringLiteral("A"),
         "remote camera was lost after SFU switch during local sharing");
  if (check_pixels) {
    QCoreApplication::processEvents();
    Expect(HasRenderedFrame(grid, QStringLiteral("A|camera")),
           "B did not render A's camera pixels after switching to SFU");
  }
  for (const auto* tile : grid.findChildren<rtcwin::ui::VideoTile*>()) {
    for (const auto* surface : {tile->mainSurface(), tile->cameraPipSurface()}) {
      Expect(surface == nullptr || surface->parentWidget() == tile,
             "SFU scene change left a surface attached to its previous tile");
    }
  }

  rtcwin::ui::VideoGrid newcomer;
  newcomer.resize(1440, 900);
  rtcwin::domain::PeerState sharer;
  sharer.uid = "B";
  sharer.screen_on = true;
  shared_state.self_uid = "C";
  shared_state.sharing = false;
  shared_state.peers = {sharer, remote};
  newcomer.SetState(shared_state);
  if (check_pixels) newcomer.show();
  auto remote_screen = MakeFrame("B", 160);
  remote_screen.source = rtcwin::domain::VideoSource::Screen;
  remote_screen.mid = "2";
  switched_frame.mid = "4";
  newcomer.SetFrame(remote_screen);
  newcomer.SetFrame(switched_frame);
  Expect(SurfaceFrameUid(newcomer, QStringLiteral("B|screen")) ==
             QStringLiteral("B") &&
             SurfaceFrameUid(newcomer, QStringLiteral("A|camera")) ==
                 QStringLiteral("A"),
         "C did not route both SFU streams to separate surfaces");
  if (check_pixels) {
    QCoreApplication::processEvents();
    Expect(HasRenderedFrame(newcomer, QStringLiteral("B|screen")) &&
               HasRenderedFrame(newcomer, QStringLiteral("A|camera")),
           "C did not render both the remote screen and camera pixels");
  }

  if (failures != 0) {
    std::cerr << failures << " video grid stress test(s) failed\n";
    return 1;
  }
  std::cout << "video_grid_stress_test: PASS\n";
  return 0;
}
