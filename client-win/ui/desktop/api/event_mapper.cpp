#include "api/event_mapper.h"

#include <QJsonArray>
#include <QJsonValue>

#include <optional>

namespace talk::desktop {
namespace {

bool HasValue(const QJsonObject& data, const char* key) {
  return data.contains(QLatin1String(key)) && !data.value(QLatin1String(key)).isNull();
}

std::optional<bool> OptionalBool(const QJsonObject& data, const char* key) {
  if (!HasValue(data, key)) return std::nullopt;
  return data.value(QLatin1String(key)).toBool();
}

std::optional<QString> OptionalString(const QJsonObject& data, const char* key) {
  if (!HasValue(data, key)) return std::nullopt;
  return data.value(QLatin1String(key)).toString();
}

ConnectionState ParseConnectionState(const QString& value) {
  if (value == QStringLiteral("connecting")) return ConnectionState::Connecting;
  if (value == QStringLiteral("ready")) return ConnectionState::Ready;
  if (value == QStringLiteral("reconnecting")) return ConnectionState::Reconnecting;
  if (value == QStringLiteral("disconnected")) return ConnectionState::Disconnected;
  if (value == QStringLiteral("failed")) return ConnectionState::Failed;
  return ConnectionState::Disconnected;
}

TalkMode ParseTalkMode(const QString& value) {
  if (value == QStringLiteral("ptt")) return TalkMode::Ptt;
  if (value == QStringLiteral("moderated")) return TalkMode::Moderated;
  return TalkMode::Free;
}

MediaModePreference ParseMediaModePreference(const QString& value) {
  if (value == QStringLiteral("sfu")) return MediaModePreference::Sfu;
  if (value == QStringLiteral("p2p")) return MediaModePreference::P2p;
  return MediaModePreference::Auto;
}

VideoSource ParseVideoSource(const QString& value) {
  return value == QStringLiteral("screen") ? VideoSource::Screen
                                            : VideoSource::Camera;
}

PeerSnapshot ParsePeer(const QJsonObject& peer) {
  PeerSnapshot result;
  result.uid = peer.value(QStringLiteral("uid")).toString();
  result.displayName = peer.value(QStringLiteral("display")).toString();
  if (result.displayName.isEmpty()) {
    result.displayName = peer.value(QStringLiteral("name")).toString(result.uid);
  }
  result.role = peer.value(QStringLiteral("role")).toString(QStringLiteral("member"));
  const QJsonObject audio = peer.value(QStringLiteral("audio")).toObject();
  result.audioMuted = audio.value(QStringLiteral("muted")).toBool(true);
  result.speaking = audio.value(QStringLiteral("speaking")).toBool(false);
  const QJsonObject video = peer.value(QStringLiteral("video")).toObject();
  result.cameraOn = video.value(QStringLiteral("camera")).toBool(false);
  result.screenOn = video.value(QStringLiteral("screen")).toBool(
      video.value(QStringLiteral("on")).toBool(false));
  return result;
}

FloorSnapshot ParseFloor(const QJsonObject& floor) {
  FloorSnapshot result;
  result.holderUid = floor.value(QStringLiteral("holder")).toString();
  result.selfHasFloor = floor.value(QStringLiteral("self")).toBool(false);
  for (const QJsonValue& value : floor.value(QStringLiteral("queue")).toArray()) {
    result.queue.append(value.toString());
  }
  result.expiresInMs = static_cast<qint64>(
      floor.value(QStringLiteral("expires_in_ms")).toDouble(0));
  return result;
}

RoomPolicy ParsePolicy(const QJsonObject& policy) {
  RoomPolicy result;
  result.talkMode = ParseTalkMode(policy.value(QStringLiteral("talk_mode")).toString());
  result.maxSpeakers = policy.value(QStringLiteral("max_speakers")).toInt(0);
  result.pttMaxHoldSeconds =
      policy.value(QStringLiteral("ptt_max_hold_sec")).toInt(60);
  result.pttQueue = policy.value(QStringLiteral("ptt_queue")).toBool(true);
  result.mediaMode = ParseMediaModePreference(
      policy.value(QStringLiteral("media_mode")).toString());
  result.p2pAllowRelay =
      policy.value(QStringLiteral("p2p_allow_relay")).toBool(true);
  result.maxPeers = policy.value(QStringLiteral("max_peers")).toInt(20);
  const QJsonObject video = policy.value(QStringLiteral("video")).toObject();
  result.videoEnabled = video.value(QStringLiteral("enabled")).toBool(true);
  result.videoMaxPublishers =
      video.value(QStringLiteral("max_publishers")).toInt(9);
  return result;
}

ResultEvent ParseResult(const QJsonObject& data) {
  ResultEvent result;
  result.command_id = data.value(QStringLiteral("id")).toVariant().toULongLong();
  result.method = data.value(QStringLiteral("method")).toString();
  result.ok = data.value(QStringLiteral("ok")).toBool(false);
  const QJsonObject error = data.value(QStringLiteral("error")).toObject();
  result.error_code = error.value(QStringLiteral("code")).toInt(0);
  result.error_message = error.value(QStringLiteral("msg")).toString();
  result.data = data.value(QStringLiteral("data")).toObject();
  return result;
}

}  // namespace

CoreEvent EventMapper::Map(const QString& name, const QJsonObject& data) {
  if (name == QStringLiteral("state")) {
    return {name, StateEvent{ParseConnectionState(
                       data.value(QStringLiteral("state")).toString())}};
  }
  if (name == QStringLiteral("ready")) {
    return {name, ReadyEvent{data.value(QStringLiteral("uid")).toString(),
                             data.value(QStringLiteral("resumed")).toBool(false)}};
  }
  if (name == QStringLiteral("joined")) {
    JoinedEvent joined;
    const QJsonObject room = data.value(QStringLiteral("room")).toObject();
    joined.room_id = room.value(QStringLiteral("room_id")).toString(
        data.value(QStringLiteral("room_id")).toString());
    joined.role = room.value(QStringLiteral("role")).toString(
        QStringLiteral("member"));
    joined.policy = ParsePolicy(room.value(QStringLiteral("policy")).toObject());
    joined.floor = ParseFloor(data.value(QStringLiteral("floor")).toObject());
    joined.media_mode = data.value(QStringLiteral("media_mode")).toString();
    for (const QJsonValue& value : data.value(QStringLiteral("peers")).toArray()) {
      joined.peers.append(ParsePeer(value.toObject()));
    }
    return {name, joined};
  }
  if (name == QStringLiteral("left") || name == QStringLiteral("room.closed")) {
    return {name, LeftEvent{}};
  }
  if (name == QStringLiteral("room.peerJoined")) {
    return {name, PeerJoinedEvent{ParsePeer(data)}};
  }
  if (name == QStringLiteral("room.peerLeft")) {
    return {name, PeerLeftEvent{data.value(QStringLiteral("uid")).toString(),
                                data.value(QStringLiteral("reason")).toString()}};
  }
  if (name == QStringLiteral("room.peerUpdated")) {
    PeerUpdatedEvent update;
    update.uid = data.value(QStringLiteral("uid")).toString();
    update.display_name = OptionalString(data, "display");
    update.role = OptionalString(data, "role");
    const QJsonObject audio = data.value(QStringLiteral("audio")).toObject();
    update.audio_muted = OptionalBool(audio, "muted");
    update.audio_speaking = OptionalBool(audio, "speaking");
    const QJsonObject video = data.value(QStringLiteral("video")).toObject();
    update.camera_on = OptionalBool(video, "camera");
    update.screen_on = OptionalBool(video, "screen");
    return {name, update};
  }
  if (name == QStringLiteral("mode.changed")) {
    return {name, ModeChangedEvent{
                      data.value(QStringLiteral("mode")).toString()}};
  }
  if (name == QStringLiteral("p2p.state")) {
    return {name, P2pStateEvent{
                      data.value(QStringLiteral("state")).toString(),
                      data.value(QStringLiteral("path")).toString(),
                      data.value(QStringLiteral("peer")).toString(),
                      data.value(QStringLiteral("role")).toString()}};
  }
  if (name == QStringLiteral("video.state")) {
    return {name, VideoStateEvent{
                      ParseVideoSource(data.value(QStringLiteral("source")).toString()),
                      data.value(QStringLiteral("sharing")).toBool(false)}};
  }
  if (name == QStringLiteral("video.track")) {
    return {name, VideoTrackEvent{
                      data.value(QStringLiteral("mid")).toString(),
                      data.value(QStringLiteral("uid")).toString(),
                      ParseVideoSource(data.value(QStringLiteral("source")).toString())}};
  }
  if (name == QStringLiteral("media.producer")) {
    return {name, ProducerStateEvent{
                      data.value(QStringLiteral("uid")).toString(),
                      data.value(QStringLiteral("mid")).toString(),
                      data.value(QStringLiteral("kind")).toString(),
                      ParseVideoSource(data.value(QStringLiteral("source")).toString()),
                      data.value(QStringLiteral("paused")).toBool(true)}};
  }
  if (name == QStringLiteral("floor.state")) {
    return {name, FloorStateEvent{ParseFloor(data)}};
  }
  if (name == QStringLiteral("floor.local")) {
    return {name, FloorLocalEvent{
                      data.value(QStringLiteral("open")).toBool(false)}};
  }
  if (name == QStringLiteral("room.policyChanged")) {
    const QJsonObject policy = data.contains(QStringLiteral("policy"))
                                   ? data.value(QStringLiteral("policy")).toObject()
                                   : data;
    return {name, PolicyChangedEvent{ParsePolicy(policy)}};
  }
  if (name == QStringLiteral("room.audioLevels")) {
    AudioLevelsEvent levels;
    for (const QJsonValue& value : data.value(QStringLiteral("levels")).toArray()) {
      const QJsonObject entry = value.toObject();
      levels.levels.append(AudioLevelEntry{
          entry.value(QStringLiteral("uid")).toString(),
          entry.value(QStringLiteral("level")).toDouble(0.0)});
    }
    return {name, levels};
  }
  if (name == QStringLiteral("room.activeSpeaker")) {
    return {name, ActiveSpeakerEvent{
                      data.value(QStringLiteral("uid")).toString()}};
  }
  if (name == QStringLiteral("media.forceMute") || name == QStringLiteral("muted")) {
    return {name, MutedEvent{name == QStringLiteral("media.forceMute")
                                 ? true
                                 : data.value(QStringLiteral("muted")).toBool(true),
                             data.value(QStringLiteral("source")).toString(
                                 QStringLiteral("mic"))}};
  }
  if (name == QStringLiteral("session.kicked")) {
    return {name, KickedEvent{data.value(QStringLiteral("reason")).toString()}};
  }
  if (name == QStringLiteral("error") || name == QStringLiteral("media.error")) {
    ErrorEvent error;
    error.name = name;
    error.code = data.value(QStringLiteral("code")).toInt(0);
    error.message = data.value(QStringLiteral("msg")).toString(
        data.value(QStringLiteral("message")).toString());
    error.detail = data.value(QStringLiteral("detail")).toString();
    return {name, error};
  }
  if (name == QStringLiteral("result")) {
    return {name, ParseResult(data)};
  }
  return {name, UnknownEvent{data}};
}

}  // namespace talk::desktop
