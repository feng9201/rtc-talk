#include "rtcwin/engine/session_state_machine.h"

#include <cstdint>
#include <iostream>
#include <string>

namespace {

using rtcwin::engine::EngineSessionSnapshot;
using rtcwin::engine::SessionMediaMode;
using rtcwin::engine::SessionPhase;
using rtcwin::engine::SessionStateMachine;

int failures = 0;

void Expect(bool condition, const char* message) {
  if (condition) return;
  ++failures;
  std::cerr << "FAIL: " << message << '\n';
}

}  // namespace

int main() {
  SessionStateMachine state;

  const std::uint64_t first = state.BeginConnect(101);
  Expect(first != 0, "first generation is zero");
  Expect(state.IsCurrent(first), "new generation is not current");
  Expect(state.TakeJoinCommand(first) == 101,
         "join command was not retained");
  Expect(state.TakeJoinCommand(first) == 0,
         "join command was returned twice");

  Expect(!state.MarkJoined(first, SessionMediaMode::Sfu, false),
         "joined accepted before hello");
  Expect(state.MarkSignalingReady(first), "hello transition failed");
  Expect(state.MarkJoiningRoom(first), "room join transition failed");
  Expect(!state.MediaModeName(SessionMediaMode::Unknown).empty(),
         "unknown mode has no name");
  Expect(state.MarkJoined(first, SessionMediaMode::P2p, false),
         "joined transition failed");
  Expect(state.SetMediaEnabled(first, true), "media enable failed");

  EngineSessionSnapshot snapshot = state.snapshot();
  Expect(snapshot.phase == SessionPhase::Joined, "phase is not joined");
  Expect(snapshot.media_mode == SessionMediaMode::P2p,
         "media mode is not p2p");
  Expect(snapshot.media_enabled, "media is not enabled");
  Expect(state.media_ready(), "media_ready is false");
  Expect(state.MarkLeaving(first), "leave transition failed");
  Expect(!state.media_ready(), "media remained enabled while leaving");

  const std::uint64_t second = state.BeginConnect(202);
  Expect(second > first, "generation did not advance");
  Expect(!state.IsCurrent(first), "old generation remained current");
  Expect(state.IsCurrent(second), "new generation is not current");
  Expect(!state.MarkSignalingReady(first), "old generation transitioned");
  Expect(state.TakeJoinCommand(second) == 202,
         "new join command was not retained");

  Expect(state.MarkFailed(second), "failed transition failed");
  Expect(!state.IsCurrent(second), "failed generation is current");
  Expect(state.IsGeneration(second), "failed generation was lost");

  state.MarkDisconnected();
  Expect(!state.IsCurrent(second), "disconnected generation is current");
  Expect(!state.IsGeneration(second), "disconnect did not advance generation");
  Expect(state.snapshot().phase == SessionPhase::Disconnected,
         "phase is not disconnected");

  if (failures != 0) {
    std::cerr << failures << " state machine test(s) failed\n";
    return 1;
  }
  std::cout << "session_state_machine_test: PASS\n";
  return 0;
}
