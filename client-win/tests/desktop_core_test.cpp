#include "api/command_correlator.h"
#include "api/core_event.h"
#include "api/event_mapper.h"
#include "api/talk_core_client.h"
#include "services/app_settings_service.h"
#include "services/session_controller.h"
#include "state/session_store.h"
#include "state/video_stream_registry.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTimer>

#include <iostream>

namespace {

int failures = 0;

void Check(bool condition, const char* message) {
  if (condition) return;
  std::cerr << "FAIL: " << message << '\n';
  ++failures;
}

void ProcessFor(int milliseconds) {
  QEventLoop loop;
  QTimer::singleShot(milliseconds, &loop, &QEventLoop::quit);
  loop.exec();
}

void TestTalkCoreClientHandle() {
  using namespace talk::desktop;
  TalkCoreClient client;
  const bool started = client.Start(
      QJsonObject{{QStringLiteral("audio_backend"), QStringLiteral("none")}});
  Check(started && client.IsRunning(), "talk core bridge start");
  const quint64 command_id = client.Submit(QStringLiteral("audio.devices"));
  Check(command_id != 0, "talk core bridge submit");
  const quint64 camera_id = client.Submit(QStringLiteral("camera.devices"));
  Check(camera_id != 0, "talk core camera command");
  client.SetLocalVideoCallback(true);
  client.SetLocalVideoCallback(false);
  client.Stop();
  Check(!client.IsRunning(), "talk core bridge stop");
}
void TestEventMapper() {
  using namespace talk::desktop;

  const CoreEvent state_event = EventMapper::Map(
      QStringLiteral("state"),
      QJsonObject{{QStringLiteral("state"), QStringLiteral("reconnecting")}});
  const auto* state = std::get_if<StateEvent>(&state_event.payload);
  Check(state != nullptr && state->state == ConnectionState::Reconnecting,
        "state event mapping");

  const CoreEvent result_event = EventMapper::Map(
      QStringLiteral("result"),
      QJsonObject{{QStringLiteral("id"), 42},
                  {QStringLiteral("method"), QStringLiteral("room.join")},
                  {QStringLiteral("ok"), false},
                  {QStringLiteral("error"),
                   QJsonObject{{QStringLiteral("code"), 409},
                               {QStringLiteral("msg"), QStringLiteral("busy")}}}});
  const auto* result = std::get_if<ResultEvent>(&result_event.payload);
  Check(result != nullptr && result->command_id == 42 && !result->ok &&
            result->error_code == 409,
        "result event mapping");

  const QJsonObject peer{{QStringLiteral("uid"), QStringLiteral("u1")},
                         {QStringLiteral("display"), QStringLiteral("张三")},
                         {QStringLiteral("role"), QStringLiteral("host")},
                         {QStringLiteral("audio"),
                          QJsonObject{{QStringLiteral("muted"), false}}},
                         {QStringLiteral("video"),
                          QJsonObject{{QStringLiteral("camera"), true},
                                      {QStringLiteral("screen"), false}}}};
  const CoreEvent joined_event = EventMapper::Map(
      QStringLiteral("joined"),
      QJsonObject{
          {QStringLiteral("media_mode"), QStringLiteral("sfu")},
          {QStringLiteral("room"),
           QJsonObject{{QStringLiteral("room_id"), QStringLiteral("r1")},
                       {QStringLiteral("role"), QStringLiteral("host")},
                       {QStringLiteral("policy"),
                        QJsonObject{{QStringLiteral("talk_mode"), QStringLiteral("ptt")},
                                    {QStringLiteral("ptt_queue"), false},
                                    {QStringLiteral("video"),
                                     QJsonObject{{QStringLiteral("enabled"), true},
                                                 {QStringLiteral("max_publishers"), 4}}}}}}},
          {QStringLiteral("peers"), QJsonArray{peer}},
          {QStringLiteral("floor"),
           QJsonObject{{QStringLiteral("holder"), QStringLiteral("u1")},
                       {QStringLiteral("queue"), QJsonArray{}},
                       {QStringLiteral("expires_in_ms"), 5000}}}});
  const auto* joined = std::get_if<JoinedEvent>(&joined_event.payload);
  Check(joined != nullptr && joined->room_id == QStringLiteral("r1") &&
            joined->role == QStringLiteral("host") &&
            joined->media_mode == QStringLiteral("sfu") &&
            joined->peers.size() == 1 && joined->peers.first().cameraOn &&
            joined->policy.talkMode == TalkMode::Ptt && !joined->policy.pttQueue &&
            joined->policy.videoMaxPublishers == 4 &&
            joined->floor.holderUid == QStringLiteral("u1") &&
            joined->floor.expiresInMs == 5000,
        "joined event mapping");
}

void TestSessionStore() {
  using namespace talk::desktop;
  SessionStore store;
  int joined_count = 0;
  int left_count = 0;
  QObject::connect(&store, &SessionStore::Joined, [&joined_count] { ++joined_count; });
  QObject::connect(&store, &SessionStore::Left, [&left_count] { ++left_count; });

  store.SetLocalIdentity(QStringLiteral("me"), QStringLiteral("本机"));
  store.ApplyEvent(EventMapper::Map(
      QStringLiteral("ready"),
      QJsonObject{{QStringLiteral("uid"), QStringLiteral("me")}}));
  store.ApplyEvent(EventMapper::Map(
      QStringLiteral("joined"),
      QJsonObject{
          {QStringLiteral("media_mode"), QStringLiteral("sfu")},
          {QStringLiteral("room"),
           QJsonObject{{QStringLiteral("room_id"), QStringLiteral("r1")},
                       {QStringLiteral("role"), QStringLiteral("host")}}},
          {QStringLiteral("peers"), QJsonArray{}},
          {QStringLiteral("floor"), QJsonObject{}}}));
  Check(store.snapshot().joined && store.snapshot().roomId == QStringLiteral("r1") &&
            store.snapshot().selfUid == QStringLiteral("me") && joined_count == 1,
        "session joined");

  store.ApplyEvent(EventMapper::Map(
      QStringLiteral("room.peerJoined"),
      QJsonObject{{QStringLiteral("uid"), QStringLiteral("u1")},
                  {QStringLiteral("display"), QStringLiteral("张三")},
                  {QStringLiteral("role"), QStringLiteral("member")}}));
  store.SetSelectedPeer(QStringLiteral("u1"));
  Check(store.snapshot().selectedUid == QStringLiteral("u1"),
        "selected peer update");
  store.ApplyEvent(EventMapper::Map(
      QStringLiteral("room.audioLevels"),
      QJsonObject{{QStringLiteral("levels"),
                   QJsonArray{QJsonObject{{QStringLiteral("uid"), QStringLiteral("u1")},
                                          {QStringLiteral("level"), 0.75}}}}}));
  Check(store.snapshot().peers.size() == 1 &&
            store.snapshot().peers.first().audioLevel > 0.7,
        "peer and audio level update");

  store.ApplyEvent(EventMapper::Map(
      QStringLiteral("floor.local"),
      QJsonObject{{QStringLiteral("open"), true}}));
  Check(!store.snapshot().micMuted &&
            store.snapshot().floor.selfHasFloor,
        "local floor opens microphone");
  store.ApplyEvent(EventMapper::Map(
      QStringLiteral("floor.local"),
      QJsonObject{{QStringLiteral("open"), false}}));
  Check(store.snapshot().micMuted, "local floor closes microphone");

  store.ApplyEvent(EventMapper::Map(
      QStringLiteral("video.track"),
      QJsonObject{{QStringLiteral("mid"), QStringLiteral("2")},
                  {QStringLiteral("uid"), QStringLiteral("u1")},
                  {QStringLiteral("source"), QStringLiteral("camera")}}));
  store.ApplyEvent(EventMapper::Map(
      QStringLiteral("media.producer"),
      QJsonObject{{QStringLiteral("uid"), QStringLiteral("u1")},
                  {QStringLiteral("mid"), QStringLiteral("2")},
                  {QStringLiteral("kind"), QStringLiteral("video")},
                  {QStringLiteral("source"), QStringLiteral("camera")},
                  {QStringLiteral("paused"), false}}));
  Check(store.snapshot().streams.size() == 1 &&
            !store.snapshot().streams.first().paused,
        "video stream state");

  store.ApplyEvent(EventMapper::Map(
      QStringLiteral("room.activeSpeaker"),
      QJsonObject{{QStringLiteral("uid"), QStringLiteral("u1")}}));
  Check(store.snapshot().activeSpeakerUid == QStringLiteral("u1"),
        "active speaker");

  store.ApplyEvent(EventMapper::Map(
      QStringLiteral("room.peerUpdated"),
      QJsonObject{{QStringLiteral("uid"), QStringLiteral("me")},
                  {QStringLiteral("role"), QStringLiteral("member")}}));
  Check(store.snapshot().selfRole == QStringLiteral("member"),
        "self peer update");

  store.ApplyEvent(EventMapper::Map(
      QStringLiteral("state"),
      QJsonObject{{QStringLiteral("state"), QStringLiteral("disconnected")}}));
  Check(!store.snapshot().joined && store.snapshot().peers.isEmpty() &&
            store.snapshot().streams.isEmpty() && store.snapshot().selfUid == QStringLiteral("me"),
        "session disconnected reset");
  Check(left_count == 1, "left emitted once");

  // A late room.closed notification must not report another leave.
  store.ApplyEvent(EventMapper::Map(QStringLiteral("room.closed"), QJsonObject{}));
  Check(left_count == 1, "duplicate room.closed is deduplicated");
}

void TestCommandCorrelator() {
  using namespace talk::desktop;
  CommandCorrelator correlator;
  bool completed = false;
  CommandResult captured;
  QObject::connect(&correlator, &CommandCorrelator::CommandCompleted,
                   [&completed, &captured](const CommandResult& result) {
                     completed = true;
                     captured = result;
                   });
  correlator.Track(7, QStringLiteral("room.join"));
  ResultEvent result;
  result.command_id = 7;
  result.ok = true;
  result.data = QJsonObject{{QStringLiteral("room_id"), QStringLiteral("r1")}};
  Check(correlator.Resolve(result), "command result resolved");
  Check(completed && captured.command_id == 7 &&
            captured.method == QStringLiteral("room.join") &&
            captured.data.value(QStringLiteral("room_id")).toString() == QStringLiteral("r1") &&
            correlator.pendingCount() == 0,
        "command completion payload");
}

void TestAppSettings() {
  using namespace talk::desktop;
  QTemporaryDir dir;
  Check(dir.isValid(), "temporary settings directory");
  if (!dir.isValid()) return;

  AppSettingsService settings(dir.filePath(QStringLiteral("settings.ini")));
  PrejoinOptions saved;
  saved.url = QStringLiteral("ws://127.0.0.1:8080/talk/v1/ws");
  saved.userId = QStringLiteral("tester");
  saved.roomId = QStringLiteral("room-a");
  saved.token = QStringLiteral("must-not-be-persisted");
  saved.fetchDevToken = true;
  saved.audioBackend = AudioBackend::Fake;
  saved.mediaMode = MediaModePreference::Sfu;
  saved.talkMode = TalkMode::Ptt;
  saved.audioSource = AudioSource::System;
  saved.volume = 73;
  saved.recordingDevice = QStringLiteral("mic-id");
  saved.playoutDevice = QStringLiteral("speaker-id");
  saved.cameraDevice = QStringLiteral("2");
  saved.cameraIndex = 2;
  settings.Save(saved);

  const PrejoinOptions loaded = settings.Load();
  Check(loaded.url == saved.url && loaded.userId == saved.userId &&
            loaded.roomId == saved.roomId && loaded.fetchDevToken &&
            loaded.audioBackend == AudioBackend::Fake &&
            loaded.mediaMode == MediaModePreference::Sfu &&
            loaded.talkMode == TalkMode::Ptt &&
            loaded.audioSource == AudioSource::System &&
            loaded.volume == 73 &&
            loaded.recordingDevice == saved.recordingDevice &&
            loaded.playoutDevice == saved.playoutDevice &&
            loaded.cameraDevice == saved.cameraDevice &&
            loaded.cameraIndex == saved.cameraIndex,
        "settings round trip");
  Check(loaded.token.isEmpty(), "token is not persisted");
}

void TestSessionControllerValidation() {
  using namespace talk::desktop;
  SessionController controller;
  QString error_message;
  bool busy = true;
  QObject::connect(
      &controller, &SessionController::ErrorOccurred,
      [&error_message](const QString& message, const QString&) {
        error_message = message;
      });
  QObject::connect(&controller, &SessionController::BusyChanged,
                   [&busy](bool value, const QString&) { busy = value; });

  PrejoinOptions options;
  options.url = QStringLiteral("wss://invalid.example/talk/v1/ws");
  options.userId = QStringLiteral("u1");
  options.roomId = QStringLiteral("r1");
  options.token = QStringLiteral("token");
  options.audioBackend = AudioBackend::None;
  controller.ConnectAndJoin(options);
  Check(!controller.busy() && !error_message.isEmpty(),
        "session controller rejects unsupported URL");
  Q_UNUSED(busy);
}

void TestVideoStreamRegistry() {
  using namespace talk::desktop;
  VideoStreamRegistry registry(nullptr, 80);
  QStringList emitted_mids;
  QStringList cleared_mids;
  QObject::connect(&registry, &VideoStreamRegistry::FrameReady,
                   [&emitted_mids](const QString& mid, const QImage& frame) {
                     if (!frame.isNull()) emitted_mids.append(mid);
                   });
  QObject::connect(&registry, &VideoStreamRegistry::FrameCleared,
                   [&cleared_mids](const QString& mid) {
                     cleared_mids.append(mid);
                   });

  registry.ApplyEvent(EventMapper::Map(
      QStringLiteral("video.track"),
      QJsonObject{{QStringLiteral("mid"), QStringLiteral("2")},
                  {QStringLiteral("uid"), QStringLiteral("u1")},
                  {QStringLiteral("source"), QStringLiteral("camera")}}));
  registry.FrameReceived(QImage(16, 9, QImage::Format_ARGB32), QStringLiteral("2"));
  ProcessFor(60);
  Check(emitted_mids.contains(QStringLiteral("2")),
        "video registry frame coalescing");
  Check(registry.streams().size() == 1 && registry.streams().first().receiving,
        "video registry receives stream");

  registry.ApplyEvent(EventMapper::Map(
      QStringLiteral("media.producer"),
      QJsonObject{{QStringLiteral("uid"), QStringLiteral("u1")},
                  {QStringLiteral("mid"), QStringLiteral("2")},
                  {QStringLiteral("kind"), QStringLiteral("video")},
                  {QStringLiteral("source"), QStringLiteral("camera")},
                  {QStringLiteral("paused"), true}}));
  Check(cleared_mids.contains(QStringLiteral("2")) &&
            registry.streams().first().paused &&
            registry.streams().first().frame.isNull(),
        "producer pause clears frame immediately");

  registry.ApplyEvent(EventMapper::Map(
      QStringLiteral("media.producer"),
      QJsonObject{{QStringLiteral("uid"), QStringLiteral("u1")},
                  {QStringLiteral("mid"), QStringLiteral("2")},
                  {QStringLiteral("kind"), QStringLiteral("video")},
                  {QStringLiteral("source"), QStringLiteral("camera")},
                  {QStringLiteral("paused"), false}}));
  registry.FrameReceived(QImage(16, 9, QImage::Format_ARGB32), QStringLiteral("2"));
  ProcessFor(180);
  Check(!registry.streams().first().receiving,
        "video registry recycles stale stream");
}

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  TestTalkCoreClientHandle();
  TestEventMapper();
  TestSessionStore();
  TestCommandCorrelator();
  TestAppSettings();
  TestSessionControllerValidation();
  TestVideoStreamRegistry();
  if (failures != 0) {
    std::cerr << failures << " test(s) failed\n";
    return 1;
  }
  std::cout << "desktop_core_test passed\n";
  return 0;
}
