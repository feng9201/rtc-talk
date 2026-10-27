#include "rtcwin/application/settings_store.h"
#include "rtcwin/client_api.h"
#include "rtcwin/domain/types.h"

#include <QCoreApplication>
#include <QTemporaryDir>

#include <iostream>
#include <string>

namespace {

int failures = 0;
int event_count = 0;
std::string last_event;

void Check(bool condition, const char* message) {
  if (condition) return;
  std::cerr << "FAIL: " << message << '\n';
  ++failures;
}

void TestDomainHelpers() {
  using namespace rtcwin::domain;
  SessionSnapshot state;
  state.connection = ConnectionPhase::Ready;
  state.join = JoinPhase::Joined;
  state.self_role = RoomRole::Host;
  Check(IsHost(state), "host role");
  Check(CanUseRoomControls(state), "room controls require ready joined");
  Check(TalkModeName(TalkMode::Ptt) == "ptt", "talk mode name");
  Check(MediaModeName(MediaModePreference::P2p) == "p2p",
        "media mode name");
  state.connection = ConnectionPhase::Connecting;
  Check(!CanUseRoomControls(state), "room controls disabled before ready");
}

void TestSettings() {
  using namespace rtcwin::application;
  QTemporaryDir dir;
  Check(dir.isValid(), "temporary settings directory");
  if (!dir.isValid()) return;
  SettingsStore store(dir.filePath(QStringLiteral("settings.ini")));
  rtcwin::domain::PrejoinOptions options;
  options.signaling_url = "wss://example.test/talk/v1/ws";
  options.user_id = "u1";
  options.room_id = "r1";
  options.token = "must-not-persist";
  options.fetch_dev_token = true;
  options.audio_backend = rtcwin::domain::AudioBackend::Fake;
  options.media_mode = rtcwin::domain::MediaModePreference::Sfu;
  options.talk_mode = rtcwin::domain::TalkMode::Ptt;
  options.volume = 77;
  store.Save(options);
  const auto loaded = store.Load();
  Check(loaded.signaling_url == options.signaling_url, "settings url");
  Check(loaded.user_id == options.user_id, "settings user");
  Check(loaded.room_id == options.room_id, "settings room");
  Check(loaded.fetch_dev_token, "settings dev token flag");
  Check(loaded.audio_backend == rtcwin::domain::AudioBackend::Fake,
        "settings audio backend");
  Check(loaded.media_mode == rtcwin::domain::MediaModePreference::Sfu,
        "settings media mode");
  Check(loaded.talk_mode == rtcwin::domain::TalkMode::Ptt,
        "settings talk mode");
  Check(loaded.volume == 77, "settings volume");
  Check(loaded.token.empty(), "settings never persist token");
}

void OnEvent(void*, const char*, const char* json) {
  ++event_count;
  last_event = json != nullptr ? json : "";
}

void TestCApi() {
  event_count = 0;
  last_event.clear();
  rtcwin_options options{};
  options.struct_size = sizeof(options);
  options.abi_version = RTCWIN_ABI_VERSION;
  options.on_event = &OnEvent;
  rtcwin_handle* handle = rtcwin_create(&options);
  Check(handle != nullptr, "C ABI create");
  if (handle == nullptr) return;
  const uint64_t ping = rtcwin_submit(handle, "engine.ping", "{}", 1000);
  Check(ping != 0 && event_count == 1, "C ABI ping");
  const uint64_t unsupported =
      rtcwin_submit(handle, "session.connectAndJoin", "{}", 1000);
  Check(unsupported != 0 && event_count == 2 &&
            last_event.find("\"ok\":false") != std::string::npos,
        "C ABI unsupported result");
  rtcwin_destroy(handle);
}

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  TestDomainHelpers();
  TestSettings();
  TestCApi();
  if (failures != 0) {
    std::cerr << failures << " test(s) failed\n";
    return 1;
  }
  std::cout << "rtcwin_tests passed\n";
  return 0;
}
