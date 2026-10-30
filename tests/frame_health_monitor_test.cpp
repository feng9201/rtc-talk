#include "rtcwin/application/frame_health_monitor.h"

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
  using rtcwin::application::FrameHealthMonitor;
  using rtcwin::application::FrameHealthState;
  using rtcwin::domain::VideoSource;
  using rtcwin::domain::VideoStreamKey;

  rtcwin::domain::SessionSnapshot snapshot;
  snapshot.self_uid = "u-self";
  snapshot.camera_on = true;

  const VideoStreamKey camera{"u-self", VideoSource::Camera};
  const VideoStreamKey screen{"u-self", VideoSource::Screen};

  FrameHealthMonitor monitor;
  monitor.Sync(snapshot);
  Expect(monitor.size() == 1, "sync should include the active camera stream");
  Expect(monitor.StateFor(camera, 1000) == FrameHealthState::Waiting,
         "stream without a frame must be waiting");

  monitor.NoteFrame(camera, "1", 1000);
  Expect(monitor.StateFor(camera, 1000) == FrameHealthState::Healthy,
         "new frame must mark the stream healthy");
  Expect(monitor.StateFor(camera, 5999) == FrameHealthState::Healthy,
         "stream became stale before five seconds");
  Expect(monitor.StateFor(camera, 6000) == FrameHealthState::Stale,
         "stream did not become stale at five seconds");
  Expect(monitor.StaleStreams(6000).size() == 1,
         "stale stream was not reported");

  monitor.NoteFrame(camera, "1", 7000);
  Expect(monitor.StateFor(camera, 8000) == FrameHealthState::Healthy,
         "new frame did not reset stale state");

  snapshot.streams.push_back(
      {"1", "u-self", VideoSource::Camera, true, false, 0});
  monitor.Sync(snapshot);
  Expect(monitor.StateFor(camera, 8000) == FrameHealthState::Paused,
         "producer_paused stream must be paused");

  snapshot.camera_on = false;
  snapshot.sharing = true;
  snapshot.streams.clear();
  monitor.Sync(snapshot);
  Expect(monitor.StateFor(camera, 8000) == FrameHealthState::Removed,
         "disabled camera stream should be removed from active metadata");
  Expect(monitor.StateFor(screen, 8000) == FrameHealthState::Waiting,
         "active screen stream should wait for its first frame");

  monitor.RemoveExcept({screen});
  Expect(monitor.size() == 1, "RemoveExcept did not prune inactive streams");
  Expect(monitor.StateFor(camera, 8000) == FrameHealthState::Removed,
         "pruned stream is still visible");

  rtcwin::domain::PeerState peer;
  peer.uid = "u-peer";
  peer.camera_on = true;
  snapshot.peers.push_back(peer);
  monitor.Sync(snapshot);
  const VideoStreamKey peer_camera{"u-peer", VideoSource::Camera};
  monitor.NoteFrame(peer_camera, "7", 9000);
  Expect(monitor.StateFor(peer_camera, 9000) == FrameHealthState::Healthy,
         "peer camera frame was not tracked");
  snapshot.peers.clear();
  monitor.Sync(snapshot);
  Expect(monitor.StateFor(peer_camera, 9000) == FrameHealthState::Removed,
         "peerLeft did not remove its video stream state");

  if (failures != 0) {
    std::cerr << failures << " frame health test(s) failed\n";
    return 1;
  }
  std::cout << "frame_health_monitor_test: PASS\n";
  return 0;
}
