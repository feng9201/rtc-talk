#include "api/command_correlator.h"
#include "api/core_event.h"
#include "api/event_mapper.h"
#include "api/talk_core_client.h"
#include "state/session_store.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonObject>

#include <iostream>

namespace {

int failures = 0;

void Check(bool condition, const char* message) {
  if (condition) return;
  std::cerr << "FAIL: " << message << '\n';
  ++failures;
}

void TestTalkCoreClientHandle() {
  using namespace talk::desktop;
  TalkCoreClient client;
  const bool started = client.Start(
      QJsonObject{{QStringLiteral("audio_backend"), QStringLiteral("none")}});
  Check(started && client.IsRunning(), "talk core bridge start");
  const quint64 command_id = client.Submit(QStringLiteral("audio.devices"));
  Check(command_id != 0, "talk core bridge submit");
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
  QObject::connect(&store, &SessionStore::Joined, [&joined_count] { ++joined_count; });

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
  store.ApplyEvent(EventMapper::Map(
      QStringLiteral("room.audioLevels"),
      QJsonObject{{QStringLiteral("levels"),
                   QJsonArray{QJsonObject{{QStringLiteral("uid"), QStringLiteral("u1")},
                                          {QStringLiteral("level"), 0.75}}}}}));
  Check(store.snapshot().peers.size() == 1 &&
            store.snapshot().peers.first().audioLevel > 0.7,
        "peer and audio level update");

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

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  TestTalkCoreClientHandle();
  TestEventMapper();
  TestSessionStore();
  TestCommandCorrelator();
  if (failures != 0) {
    std::cerr << failures << " test(s) failed\n";
    return 1;
  }
  std::cout << "desktop_core_test passed\n";
  return 0;
}
