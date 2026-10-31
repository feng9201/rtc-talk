#include "rtcwin/application/floor_controller.h"
#include "rtcwin/application/host_controller.h"

#include <iostream>
#include <string>
#include <vector>

namespace {

int failures = 0;

void Expect(bool condition, const char* message) {
  if (condition) return;
  ++failures;
  std::cerr << "FAIL: " << message << '\n';
}

class FakeEngineClient final : public rtcwin::application::EngineClient {
 public:
  void ConnectAndJoin(const rtcwin::domain::PrejoinOptions&) override {}
  void LeaveRoom() override {}
  void Disconnect() override {}
  void SetMicEnabled(bool) override {}
  void ToggleCamera() override {}
  void ToggleShare() override {}
  void SetDeafened(bool) override {}
  void RequestFloor() override { ++floor_requests; }
  void ReleaseFloor() override { ++floor_releases; }
  void GrantFloor(const std::string& uid, bool allow) override {
    grant_uid = uid;
    grant_allow = allow;
  }
  void MuteAll(const std::vector<std::string>& except) override {
    mute_all_except = except;
  }
  void KickMember(const std::string& uid) override { kick_uid = uid; }
  void UpdateRoomPolicy(
      const rtcwin::domain::RoomPolicyPatch& patch) override {
    policy_patch = patch;
  }
  void SubscribeLevels(bool enabled) override {
    levels_enabled = enabled;
  }
  void SelectPeer(const std::string&) override {}
  void RefreshCameras() override {}
  void RefreshAudioDevices() override {}
  void RefreshDesktopSources() override {}
  void SelectCamera(int) override {}
  void SelectRecordingDevice(const std::string&) override {}
  void SelectPlayoutDevice(const std::string&) override {}
  void SelectSystemAudioDevice(const std::string&) override {}
  void SelectScreenSource(const std::string&, bool) override {}
  void ForceReconnect() override {}
  void RefreshIce() override {}
  void RestartIce(const std::string&) override {}
  void StartCameraPreview(int) override {}
  void StopCameraPreview() override {}

  int floor_requests = 0;
  int floor_releases = 0;
  std::string grant_uid;
  bool grant_allow = false;
  std::vector<std::string> mute_all_except;
  std::string kick_uid;
  rtcwin::domain::RoomPolicyPatch policy_patch;
  bool levels_enabled = false;
};

rtcwin::domain::SessionSnapshot JoinedSnapshot() {
  rtcwin::domain::SessionSnapshot snapshot;
  snapshot.connection = rtcwin::domain::ConnectionPhase::Ready;
  snapshot.join = rtcwin::domain::JoinPhase::Joined;
  snapshot.self_uid = "self";
  snapshot.talk_mode = rtcwin::domain::TalkMode::Ptt;
  snapshot.policy.talk_mode = snapshot.talk_mode;
  return snapshot;
}

}  // namespace

int main() {
  FakeEngineClient engine;
  rtcwin::application::FloorController floor(&engine);

  floor.Sync(JoinedSnapshot());
  Expect(floor.Request(), "PTT floor request was rejected");
  Expect(floor.Release(), "PTT floor release was rejected");
  Expect(engine.floor_requests == 1 && engine.floor_releases == 1,
         "floor commands were not sent to the engine");

  auto toned = JoinedSnapshot();
  toned.talk_mode = rtcwin::domain::TalkMode::Moderated;
  toned.policy.talk_mode = toned.talk_mode;
  floor.Sync(toned);
  Expect(floor.Toggle(), "moderated floor request was rejected");
  toned.floor.self_has_floor = true;
  floor.Sync(toned);
  Expect(floor.Toggle(), "moderated floor release was rejected");
  Expect(engine.floor_requests == 2 && engine.floor_releases == 2,
         "moderated toggle did not request then release");

  auto free = JoinedSnapshot();
  free.talk_mode = rtcwin::domain::TalkMode::Free;
  free.policy.talk_mode = free.talk_mode;
  floor.Sync(free);
  Expect(!floor.Request(), "free mode incorrectly requested a floor");

  rtcwin::application::HostController host(&engine);
  Expect(host.Grant("peer", true), "host grant was rejected");
  Expect(engine.grant_uid == "peer" && engine.grant_allow,
         "host grant payload was incorrect");
  Expect(host.MuteAll({"peer"}), "host muteAll was rejected");
  Expect(engine.mute_all_except == std::vector<std::string>{"peer"},
         "host muteAll except list was incorrect");
  Expect(host.Kick("peer"), "host kick was rejected");
  Expect(engine.kick_uid == "peer", "host kick payload was incorrect");

  rtcwin::domain::RoomPolicyPatch patch;
  patch.talk_mode = rtcwin::domain::TalkMode::Moderated;
  patch.max_speakers = 4;
  host.UpdatePolicy(patch);
  Expect(engine.policy_patch.talk_mode.has_value() &&
             *engine.policy_patch.talk_mode ==
                 rtcwin::domain::TalkMode::Moderated,
         "policy talk mode was not forwarded");
  Expect(engine.policy_patch.max_speakers.has_value() &&
             *engine.policy_patch.max_speakers == 4,
         "policy speaker limit was not forwarded");

  if (failures != 0) {
    std::cerr << failures << " floor/host controller test(s) failed\n";
    return 1;
  }
  std::cout << "floor_host_controller_test: PASS\n";
  return 0;
}
