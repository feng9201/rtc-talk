#include "rtcwin/client_gateway/c_abi_engine_client.h"

#include "rtcwin/client_api.h"

#include <QByteArray>
#include <QDateTime>
#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QMetaObject>
#include <QTimer>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <QUrlQuery>

#include <algorithm>
#include <cstring>
#include <utility>

namespace rtcwin::client_gateway {
namespace {

// 联调诊断：RTCWIN_TRACE_EVENTS=1 时在 stderr 打印引擎事件。
// 事件信封不含 token、SDP、ICE 凭证，符合 handover §13.2 的日志红线。
bool EventTraceEnabled() {
  static const bool enabled = [] {
    const QByteArray value =
        qgetenv("RTCWIN_TRACE_EVENTS").trimmed().toLower();
    return value == "1" || value == "true" || value == "yes" ||
           value == "on";
  }();
  return enabled;
}

bool SensitiveTraceKey(const QString& key) {
  const QString lower = key.toLower();
  return lower == QStringLiteral("token") ||
         lower == QStringLiteral("authorization") ||
         lower == QStringLiteral("credential") ||
         lower == QStringLiteral("password") ||
         lower == QStringLiteral("sdp") ||
         lower == QStringLiteral("candidate");
}

QJsonValue RedactTraceValue(const QJsonValue& value) {
  if (value.isObject()) {
    const QJsonObject source = value.toObject();
    QJsonObject result;
    for (auto it = source.begin(); it != source.end(); ++it) {
      result.insert(it.key(), SensitiveTraceKey(it.key())
                                  ? QJsonValue(QStringLiteral("<redacted>"))
                                  : RedactTraceValue(it.value()));
    }
    return result;
  }
  if (value.isArray()) {
    QJsonArray result;
    for (const QJsonValue& item : value.toArray()) {
      result.push_back(RedactTraceValue(item));
    }
    return result;
  }
  return value;
}

QJsonObject TraceObject(const QJsonObject& object) {
  return RedactTraceValue(object).toObject();
}

domain::VideoFrame CopyFrame(const rtcwin_frame_view& view) {
  domain::VideoFrame frame;
  frame.uid = view.uid != nullptr ? view.uid : "";
  frame.mid = view.mid != nullptr ? view.mid : "";
  frame.source = view.source == 1 ? domain::VideoSource::Screen
                                   : domain::VideoSource::Camera;
  frame.width = view.width;
  frame.height = view.height;
  frame.stride_y = view.stride_y;
  frame.stride_u = view.stride_u;
  frame.stride_v = view.stride_v;
  frame.timestamp_us = view.timestamp_us;
  if (view.i420_y == nullptr || view.i420_u == nullptr ||
      view.i420_v == nullptr || view.width <= 0 || view.height <= 0 ||
      view.stride_y < view.width || view.stride_u < (view.width + 1) / 2 ||
      view.stride_v < (view.width + 1) / 2) {
    return {};
  }
  const int chroma_height = (view.height + 1) / 2;
  frame.y.resize(static_cast<size_t>(view.stride_y) * view.height);
  frame.u.resize(static_cast<size_t>(view.stride_u) * chroma_height);
  frame.v.resize(static_cast<size_t>(view.stride_v) * chroma_height);
  std::memcpy(frame.y.data(), view.i420_y, frame.y.size());
  std::memcpy(frame.u.data(), view.i420_u, frame.u.size());
  std::memcpy(frame.v.data(), view.i420_v, frame.v.size());
  return frame;
}

domain::RoomRole ParseRole(const QString& role) {
  return role == QStringLiteral("host") ? domain::RoomRole::Host
                                        : domain::RoomRole::Member;
}

domain::TalkMode ParseTalkMode(const QString& mode) {
  if (mode == QStringLiteral("ptt")) return domain::TalkMode::Ptt;
  if (mode == QStringLiteral("moderated")) return domain::TalkMode::Moderated;
  return domain::TalkMode::Free;
}

void ApplyPolicy(const QJsonObject& policy, domain::RoomPolicy* target) {
  if (target == nullptr || policy.isEmpty()) return;
  if (policy.contains(QStringLiteral("talk_mode"))) {
    target->talk_mode =
        ParseTalkMode(policy.value(QStringLiteral("talk_mode")).toString());
  }
  if (policy.contains(QStringLiteral("max_speakers"))) {
    target->max_speakers =
        policy.value(QStringLiteral("max_speakers")).toInt(target->max_speakers);
  }
  if (policy.contains(QStringLiteral("ptt_max_hold_sec"))) {
    target->ptt_max_hold_seconds =
        policy.value(QStringLiteral("ptt_max_hold_sec"))
            .toInt(target->ptt_max_hold_seconds);
  }
  if (policy.contains(QStringLiteral("ptt_queue"))) {
    target->ptt_queue =
        policy.value(QStringLiteral("ptt_queue")).toBool(target->ptt_queue);
  }
  if (policy.contains(QStringLiteral("media_mode"))) {
    const QString media_mode =
        policy.value(QStringLiteral("media_mode")).toString();
    target->media_mode =
        media_mode == QStringLiteral("sfu")
            ? domain::MediaModePreference::Sfu
            : media_mode == QStringLiteral("p2p")
                  ? domain::MediaModePreference::P2p
                  : domain::MediaModePreference::Auto;
  }
  if (policy.contains(QStringLiteral("p2p_allow_relay"))) {
    target->p2p_allow_relay =
        policy.value(QStringLiteral("p2p_allow_relay"))
            .toBool(target->p2p_allow_relay);
  }
  if (policy.contains(QStringLiteral("max_peers"))) {
    target->max_peers =
        policy.value(QStringLiteral("max_peers")).toInt(target->max_peers);
  }
  if (policy.contains(QStringLiteral("video_enabled"))) {
    target->video_enabled =
        policy.value(QStringLiteral("video_enabled"))
            .toBool(target->video_enabled);
  }
  if (policy.contains(QStringLiteral("video_max_publishers"))) {
    target->video_max_publishers =
        policy.value(QStringLiteral("video_max_publishers"))
            .toInt(target->video_max_publishers);
  }
  const QJsonObject video = policy.value(QStringLiteral("video")).toObject();
  if (video.contains(QStringLiteral("enabled"))) {
    target->video_enabled =
        video.value(QStringLiteral("enabled")).toBool(target->video_enabled);
  }
  if (video.contains(QStringLiteral("max_publishers"))) {
    target->video_max_publishers =
        video.value(QStringLiteral("max_publishers"))
            .toInt(target->video_max_publishers);
  }
}

domain::PeerState ParsePeer(const QJsonObject& object) {
  domain::PeerState peer;
  peer.uid = object.value(QStringLiteral("uid")).toString().toStdString();
  peer.display_name = object.value(QStringLiteral("display"))
                          .toString(QString::fromStdString(peer.uid))
                          .toStdString();
  peer.role = ParseRole(object.value(QStringLiteral("role")).toString());
  const QJsonObject audio = object.value(QStringLiteral("audio")).toObject();
  peer.audio_muted = audio.value(QStringLiteral("muted")).toBool(true);
  peer.speaking = audio.value(QStringLiteral("speaking")).toBool(false);
  peer.audio_level = audio.value(QStringLiteral("level")).toDouble(0.0);
  const QJsonObject video = object.value(QStringLiteral("video")).toObject();
  peer.camera_on = video.value(QStringLiteral("camera")).toBool(
      video.value(QStringLiteral("on")).toBool(false));
  peer.screen_on = video.value(QStringLiteral("screen")).toBool(false);
  return peer;
}

void UpsertPeer(std::vector<domain::PeerState>* peers,
                const domain::PeerState& peer) {
  if (peer.uid.empty()) return;
  for (domain::PeerState& current : *peers) {
    if (current.uid == peer.uid) {
      current = peer;
      return;
    }
  }
  peers->push_back(peer);
}

}  // namespace

struct CAbiEngineClient::CallbackContext {
  CAbiEngineClient* self = nullptr;
};

CAbiEngineClient::CAbiEngineClient(QObject* parent) : EngineClient(parent) {
  context_ = std::make_unique<CallbackContext>();
  context_->self = this;
  rtcwin_options options{};
  options.struct_size = sizeof(options);
  options.abi_version = RTCWIN_ABI_VERSION;
  options.userdata = context_.get();
  options.on_event = &CAbiEngineClient::OnEvent;
  options.on_remote_frame = &CAbiEngineClient::OnRemoteFrame;
  options.on_local_frame = &CAbiEngineClient::OnLocalFrame;
  network_ = new QNetworkAccessManager(this);
  handle_ = rtcwin_create(&options);
  // Engine writes this small diagnostic snapshot to the rolling file log.
  trace_stats_timer_ = new QTimer(this);
  trace_stats_timer_->setInterval(5000);
  connect(trace_stats_timer_, &QTimer::timeout, this, [this] {
    if (handle_ != nullptr &&
        snapshot_.join == domain::JoinPhase::Joined) {
      Submit(QStringLiteral("media.stats"));
    }
  });
  trace_stats_timer_->start();

}

CAbiEngineClient::~CAbiEngineClient() {
  if (handle_ != nullptr) {
    rtcwin_destroy(handle_);
    handle_ = nullptr;
  }
  context_.reset();
}

void CAbiEngineClient::ConnectAndJoin(
    const domain::PrejoinOptions& options) {
  snapshot_ = {};
  snapshot_.room_id = options.room_id;
  snapshot_.self_uid = options.user_id;
  snapshot_.self_display_name = options.user_id;
  snapshot_.talk_mode = options.talk_mode;
  snapshot_.policy.talk_mode = options.talk_mode;
  snapshot_.connection = domain::ConnectionPhase::Connecting;
  snapshot_.join = domain::JoinPhase::Joining;
  if (!options.fetch_dev_token || !options.token.empty()) {
    SubmitConnectAndJoin(options, QString::fromStdString(options.token));
    return;
  }

  const QUrl signaling(QString::fromStdString(options.signaling_url));
  QUrl endpoint;
  endpoint.setScheme(signaling.scheme() == QStringLiteral("wss")
                         ? QStringLiteral("https")
                         : QStringLiteral("http"));
  endpoint.setHost(signaling.host());
  endpoint.setPort(signaling.port());
  endpoint.setPath(QStringLiteral("/dev/token"));
  QUrlQuery query;
  query.addQueryItem(QStringLiteral("uid"),
                     QString::fromStdString(options.user_id));
  query.addQueryItem(QStringLiteral("rooms"),
                     QString::fromStdString(options.room_id));
  endpoint.setQuery(query);

  QNetworkReply* reply = network_->get(QNetworkRequest(endpoint));
  connect(reply, &QNetworkReply::finished, this,
          [this, reply, options] {
            const QByteArray body = reply->readAll();
            const QNetworkReply::NetworkError reply_error = reply->error();
            const QString reply_text = reply->errorString();
            reply->deleteLater();
            if (reply_error != QNetworkReply::NoError) {
              domain::AppError error;
              error.domain = domain::ErrorDomain::Network;
              error.code = 1102;
              error.operation = "dev_token";
              error.message = "获取开发 Token 失败";
              error.detail = reply_text.toStdString();
              emit ErrorOccurred(error);
              return;
            }
            const QJsonObject object =
                QJsonDocument::fromJson(body).object();
            const QString token =
                object.value(QStringLiteral("token")).toString();
            if (token.isEmpty()) {
              domain::AppError error;
              error.domain = domain::ErrorDomain::Authentication;
              error.code = 1103;
              error.operation = "dev_token";
              error.message = "开发 Token 响应无效";
              error.detail = "missing token";
              emit ErrorOccurred(error);
              return;
            }
            SubmitConnectAndJoin(options, token);
          });
}

void CAbiEngineClient::ApplyJoinData(const QJsonObject& data) {
  snapshot_.connection = domain::ConnectionPhase::Ready;
  snapshot_.join = domain::JoinPhase::Joined;
  snapshot_.media_enabled =
      data.value(QStringLiteral("media_enabled")).toBool(false);
  snapshot_.media_mode =
      data.value(QStringLiteral("media_mode")).toString().toStdString();
  const QJsonObject room = data.value(QStringLiteral("room")).toObject();
  if (!room.value(QStringLiteral("room_id")).toString().isEmpty()) {
    snapshot_.room_id =
        room.value(QStringLiteral("room_id")).toString().toStdString();
  }
  snapshot_.self_role =
      ParseRole(room.value(QStringLiteral("role")).toString());
  const QJsonObject policy = room.value(QStringLiteral("policy")).toObject();
  ApplyPolicy(policy, &snapshot_.policy);
  snapshot_.talk_mode = snapshot_.policy.talk_mode;
  if (snapshot_.talk_mode != domain::TalkMode::Free) {
    snapshot_.mic_muted = true;
  }
  const QJsonObject floor = data.value(QStringLiteral("floor")).toObject();
  snapshot_.floor.holder_uid =
      floor.value(QStringLiteral("holder")).toString().toStdString();
  snapshot_.floor.queue.clear();
  for (const QJsonValue& value :
       floor.value(QStringLiteral("queue")).toArray()) {
    snapshot_.floor.queue.push_back(value.toString().toStdString());
  }
  snapshot_.floor.expires_in_ms =
      static_cast<qint64>(floor.value(QStringLiteral("expires_in_ms"))
                              .toDouble(0));
  if (snapshot_.floor.expires_in_ms <= 0 &&
      floor.value(QStringLiteral("expires_at")).isDouble()) {
    snapshot_.floor.expires_in_ms = std::max<qint64>(
        0, static_cast<qint64>(
               floor.value(QStringLiteral("expires_at")).toDouble()) -
               QDateTime::currentMSecsSinceEpoch());
  }
  snapshot_.floor.deadline_epoch_ms =
      floor.value(QStringLiteral("expires_at")).isDouble()
          ? static_cast<qint64>(
                floor.value(QStringLiteral("expires_at")).toDouble())
          : (snapshot_.floor.expires_in_ms > 0
                 ? QDateTime::currentMSecsSinceEpoch() +
                       snapshot_.floor.expires_in_ms
                 : 0);
  snapshot_.floor.self_has_floor =
      floor.value(QStringLiteral("self")).toBool(
          snapshot_.floor.holder_uid == snapshot_.self_uid);
  snapshot_.peers.clear();
  for (const QJsonValue& value :
       data.value(QStringLiteral("peers")).toArray()) {
    const domain::PeerState peer = ParsePeer(value.toObject());
    if (!peer.uid.empty() && peer.uid != snapshot_.self_uid) {
      UpsertPeer(&snapshot_.peers, peer);
    }
  }
  emit SnapshotChanged(snapshot_);
}

void CAbiEngineClient::ApplyRoomNotification(const QString& method,
                                             const QJsonObject& data) {
  if (method == QStringLiteral("room.peerJoined")) {
    const domain::PeerState peer = ParsePeer(data);
    if (!peer.uid.empty() && peer.uid != snapshot_.self_uid) {
      UpsertPeer(&snapshot_.peers, peer);
    }
  } else if (method == QStringLiteral("room.peerLeft")) {
    const std::string uid =
        data.value(QStringLiteral("uid")).toString().toStdString();
    for (auto it = snapshot_.peers.begin(); it != snapshot_.peers.end(); ++it) {
      if (it->uid == uid) {
        snapshot_.peers.erase(it);
        break;
      }
    }
    for (auto it = snapshot_.streams.begin();
         it != snapshot_.streams.end();) {
      if (it->uid == uid) {
        it = snapshot_.streams.erase(it);
      } else {
        ++it;
      }
    }
  } else if (method == QStringLiteral("room.peerUpdated")) {
    const std::string uid =
        data.value(QStringLiteral("uid")).toString().toStdString();
    const bool is_self = uid == snapshot_.self_uid;
    domain::PeerState* peer = nullptr;
    if (is_self) {
      if (data.contains(QStringLiteral("role"))) {
        snapshot_.self_role =
            ParseRole(data.value(QStringLiteral("role")).toString());
      }
      if (data.contains(QStringLiteral("display"))) {
        snapshot_.self_display_name =
            data.value(QStringLiteral("display"))
                .toString(QString::fromStdString(snapshot_.self_uid))
                .toStdString();
      }
      const QJsonObject audio = data.value(QStringLiteral("audio")).toObject();
      if (audio.contains(QStringLiteral("muted"))) {
        snapshot_.mic_muted =
            audio.value(QStringLiteral("muted")).toBool(snapshot_.mic_muted);
      }
      const QJsonObject video = data.value(QStringLiteral("video")).toObject();
      if (video.contains(QStringLiteral("camera"))) {
        snapshot_.camera_on = video.value(QStringLiteral("camera"))
                                  .toBool(snapshot_.camera_on);
      }
      if (video.contains(QStringLiteral("screen"))) {
        snapshot_.sharing = video.value(QStringLiteral("screen"))
                                .toBool(snapshot_.sharing);
      }
    } else {
      for (domain::PeerState& current : snapshot_.peers) {
        if (current.uid == uid) {
          peer = &current;
          break;
        }
      }
      if (peer == nullptr && !uid.empty()) {
        domain::PeerState created;
        created.uid = uid;
        created.display_name = data.value(QStringLiteral("display"))
                                   .toString(QString::fromStdString(uid))
                                   .toStdString();
        snapshot_.peers.push_back(created);
        peer = &snapshot_.peers.back();
        if (data.contains(QStringLiteral("role"))) {
          peer->role = ParseRole(data.value(QStringLiteral("role")).toString());
        }
      }
    }
    if (peer != nullptr && data.contains(QStringLiteral("audio"))) {
      const QJsonObject audio = data.value(QStringLiteral("audio")).toObject();
      if (audio.contains(QStringLiteral("muted"))) {
        peer->audio_muted =
            audio.value(QStringLiteral("muted")).toBool(peer->audio_muted);
      }
      if (audio.contains(QStringLiteral("speaking"))) {
        peer->speaking =
            audio.value(QStringLiteral("speaking")).toBool(peer->speaking);
      }
      if (audio.contains(QStringLiteral("level"))) {
        peer->audio_level =
            audio.value(QStringLiteral("level")).toDouble(peer->audio_level);
      }
    }
    if (peer != nullptr && data.contains(QStringLiteral("video"))) {
      const QJsonObject video = data.value(QStringLiteral("video")).toObject();
      if (video.contains(QStringLiteral("camera"))) {
        peer->camera_on =
            video.value(QStringLiteral("camera")).toBool(peer->camera_on);
      }
      if (video.contains(QStringLiteral("screen"))) {
        peer->screen_on =
            video.value(QStringLiteral("screen")).toBool(peer->screen_on);
      }
    }
  } else if (method == QStringLiteral("room.policyChanged")) {
    const QJsonObject policy =
        data.contains(QStringLiteral("policy"))
            ? data.value(QStringLiteral("policy")).toObject()
            : data;
    ApplyPolicy(policy, &snapshot_.policy);
    snapshot_.talk_mode = snapshot_.policy.talk_mode;
  } else if (method == QStringLiteral("room.mediaMode")) {
    snapshot_.media_mode =
        data.value(QStringLiteral("mode")).toString().toStdString();
  } else if (method == QStringLiteral("floor.state")) {
    snapshot_.floor.holder_uid =
        data.value(QStringLiteral("holder")).toString().toStdString();
    snapshot_.floor.queue.clear();
    for (const QJsonValue& value :
         data.value(QStringLiteral("queue")).toArray()) {
      snapshot_.floor.queue.push_back(value.toString().toStdString());
    }
    snapshot_.floor.expires_in_ms =
        static_cast<qint64>(data.value(QStringLiteral("expires_in_ms"))
                                .toDouble(0));
    if (snapshot_.floor.expires_in_ms <= 0 &&
        data.value(QStringLiteral("expires_at")).isDouble()) {
      snapshot_.floor.expires_in_ms = std::max<qint64>(
          0, static_cast<qint64>(
                 data.value(QStringLiteral("expires_at")).toDouble()) -
                 QDateTime::currentMSecsSinceEpoch());
    }
    snapshot_.floor.deadline_epoch_ms =
        data.value(QStringLiteral("expires_at")).isDouble()
            ? static_cast<qint64>(
                  data.value(QStringLiteral("expires_at")).toDouble())
            : (snapshot_.floor.expires_in_ms > 0
                   ? QDateTime::currentMSecsSinceEpoch() +
                         snapshot_.floor.expires_in_ms
                   : 0);
    snapshot_.floor.self_has_floor =
        data.value(QStringLiteral("self")).toBool(
            snapshot_.floor.holder_uid == snapshot_.self_uid);
  } else if (method == QStringLiteral("floor.local")) {
    snapshot_.floor.self_has_floor =
        data.value(QStringLiteral("open")).toBool(false);
    snapshot_.mic_muted = !snapshot_.floor.self_has_floor;
  } else if (method == QStringLiteral("floor.revoked")) {
    snapshot_.floor.self_has_floor = false;
    snapshot_.floor.holder_uid.clear();
    snapshot_.floor.expires_in_ms = 0;
    snapshot_.floor.deadline_epoch_ms = 0;
    snapshot_.mic_muted = true;
  } else if (method == QStringLiteral("room.activeSpeaker")) {
    const std::string uid =
        data.value(QStringLiteral("uid")).toString().toStdString();
    for (domain::PeerState& peer : snapshot_.peers) {
      peer.speaking = peer.uid == uid;
    }
  } else if (method == QStringLiteral("room.audioLevels")) {
    snapshot_.self_audio_level = 0.0;
    for (domain::PeerState& peer : snapshot_.peers) {
      peer.audio_level = 0.0;
    }
    for (const QJsonValue& value :
         data.value(QStringLiteral("levels")).toArray()) {
      const QJsonObject level = value.toObject();
      const std::string uid =
          level.value(QStringLiteral("uid")).toString().toStdString();
      if (uid == snapshot_.self_uid) {
        snapshot_.self_audio_level =
            level.value(QStringLiteral("level")).toDouble();
        continue;
      }
      for (domain::PeerState& peer : snapshot_.peers) {
        if (peer.uid == uid) {
          peer.audio_level =
              level.value(QStringLiteral("level")).toDouble();
          break;
        }
      }
    }
  } else if (method == QStringLiteral("session.kicked")) {
    const QString reason =
        data.value(QStringLiteral("reason")).toString();
    domain::AppError error;
    error.domain = domain::ErrorDomain::Session;
    error.code = reason == QStringLiteral("replaced") ? 4101 : 4102;
    error.operation = "session.kicked";
    error.message = reason == QStringLiteral("replaced")
                        ? "账号已在其他客户端登录"
                        : "已被移出房间";
    snapshot_ = {};
    emit SnapshotChanged(snapshot_);
    emit ErrorOccurred(error);
    emit Left();
    return;
  } else if (method == QStringLiteral("room.closed")) {
    snapshot_ = {};
    emit SnapshotChanged(snapshot_);
    emit Left();
    return;
  } else if (method == QStringLiteral("session.closed")) {
    if (snapshot_.join == domain::JoinPhase::Joined) {
      snapshot_ = {};
      emit SnapshotChanged(snapshot_);
      emit Left();
    }
    return;
  } else {
    return;
  }
  emit SnapshotChanged(snapshot_);
}
void CAbiEngineClient::SubmitConnectAndJoin(
    const domain::PrejoinOptions& options, const QString& token) {
  QJsonObject payload;
  payload.insert(QStringLiteral("url"),
                 QString::fromStdString(options.signaling_url));
  payload.insert(QStringLiteral("user_id"),
                 QString::fromStdString(options.user_id));
  payload.insert(QStringLiteral("room_id"),
                 QString::fromStdString(options.room_id));
  payload.insert(QStringLiteral("token"), token);
  payload.insert(QStringLiteral("fetch_dev_token"), options.fetch_dev_token);
  payload.insert(QStringLiteral("volume"), options.volume);
  payload.insert(QStringLiteral("recording_device"),
                 QString::fromStdString(options.recording_device));
  payload.insert(QStringLiteral("playout_device"),
                 QString::fromStdString(options.playout_device));
  payload.insert(QStringLiteral("system_audio_device"),
                 QString::fromStdString(options.system_audio_device));
  payload.insert(QStringLiteral("camera_device"),
                 QString::fromStdString(options.camera_device));
  QString audio_backend = QStringLiteral("wasapi");
  if (options.audio_backend == domain::AudioBackend::Fake) {
    audio_backend = QStringLiteral("fake");
  } else if (options.audio_backend == domain::AudioBackend::None) {
    audio_backend = QStringLiteral("none");
  }
  payload.insert(QStringLiteral("audio_backend"), audio_backend);
  payload.insert(
      QStringLiteral("audio_source"),
      options.audio_source == domain::AudioSource::System
          ? QStringLiteral("system")
          : QStringLiteral("mic"));
  QString media_mode = QStringLiteral("auto");
  if (options.media_mode == domain::MediaModePreference::Sfu) {
    media_mode = QStringLiteral("sfu");
  } else if (options.media_mode == domain::MediaModePreference::P2p) {
    media_mode = QStringLiteral("p2p");
  }
  payload.insert(QStringLiteral("media_mode"), media_mode);
  payload.insert(QStringLiteral("talk_mode"),
                 QString::fromStdString(domain::TalkModeName(options.talk_mode)));
  Submit(QStringLiteral("session.connectAndJoin"), payload);
}

void CAbiEngineClient::LeaveRoom() { Submit(QStringLiteral("room.leave")); }
void CAbiEngineClient::Disconnect() {
  Submit(QStringLiteral("session.disconnect"));
}
void CAbiEngineClient::SetMicEnabled(bool enabled) {
  Submit(QStringLiteral("media.setMute"),
         QJsonObject{{QStringLiteral("muted"), !enabled}});
}
void CAbiEngineClient::ToggleCamera() {
  Submit(QStringLiteral("media.toggleCamera"));
}
void CAbiEngineClient::ToggleShare() {
  Submit(QStringLiteral("media.toggleScreenShare"));
}
void CAbiEngineClient::SetDeafened(bool deafened) {
  Submit(QStringLiteral("media.setDeafened"),
         QJsonObject{{QStringLiteral("deafened"), deafened}});
}
void CAbiEngineClient::RequestFloor() {
  Submit(QStringLiteral("floor.request"));
}
void CAbiEngineClient::ReleaseFloor() {
  Submit(QStringLiteral("floor.release"));
}
void CAbiEngineClient::GrantFloor(const std::string& uid, bool allow) {
  Submit(QStringLiteral("host.grant"),
         QJsonObject{{QStringLiteral("uid"),
                      QString::fromStdString(uid)},
                     {QStringLiteral("allow"), allow}});
}
void CAbiEngineClient::MuteAll(const std::vector<std::string>& except) {
  QJsonArray excluded;
  for (const auto& uid : except) {
    excluded.push_back(QString::fromStdString(uid));
  }
  Submit(QStringLiteral("host.muteAll"),
         QJsonObject{{QStringLiteral("except"), excluded}});
}
void CAbiEngineClient::KickMember(const std::string& uid) {
  Submit(QStringLiteral("host.kick"),
         QJsonObject{{QStringLiteral("uid"),
                      QString::fromStdString(uid)}});
}
void CAbiEngineClient::UpdateRoomPolicy(
    const domain::RoomPolicyPatch& patch) {
  QJsonObject payload;
  if (patch.talk_mode.has_value()) {
    payload.insert(QStringLiteral("talk_mode"),
                   QString::fromStdString(
                       domain::TalkModeName(*patch.talk_mode)));
  }
  if (patch.max_speakers.has_value()) {
    payload.insert(QStringLiteral("max_speakers"), *patch.max_speakers);
  }
  if (patch.ptt_max_hold_seconds.has_value()) {
    payload.insert(QStringLiteral("ptt_max_hold_sec"),
                   *patch.ptt_max_hold_seconds);
  }
  if (patch.ptt_queue.has_value()) {
    payload.insert(QStringLiteral("ptt_queue"), *patch.ptt_queue);
  }
  if (patch.media_mode.has_value()) {
    payload.insert(QStringLiteral("media_mode"),
                   QString::fromStdString(
                       domain::MediaModeName(*patch.media_mode)));
  }
  if (patch.p2p_allow_relay.has_value()) {
    payload.insert(QStringLiteral("p2p_allow_relay"),
                   *patch.p2p_allow_relay);
  }
  if (patch.max_peers.has_value()) {
    payload.insert(QStringLiteral("max_peers"), *patch.max_peers);
  }
  QJsonObject video;
  if (patch.video_enabled.has_value()) {
    video.insert(QStringLiteral("enabled"), *patch.video_enabled);
  }
  if (patch.video_max_publishers.has_value()) {
    video.insert(QStringLiteral("max_publishers"),
                 *patch.video_max_publishers);
  }
  if (!video.isEmpty()) {
    payload.insert(QStringLiteral("video"), video);
  }
  Submit(QStringLiteral("host.setPolicy"), payload);
}
void CAbiEngineClient::SubscribeLevels(bool enabled) {
  Submit(QStringLiteral("room.subscribeLevels"),
         QJsonObject{{QStringLiteral("enabled"), enabled}});
}
void CAbiEngineClient::SelectPeer(const std::string& uid) { Q_UNUSED(uid); }
void CAbiEngineClient::RefreshCameras() {
  Submit(QStringLiteral("camera.devices"));
}
void CAbiEngineClient::RefreshAudioDevices() {
  Submit(QStringLiteral("audio.devices"));
}
void CAbiEngineClient::RefreshDesktopSources() {
  Submit(QStringLiteral("desktop.sources"));
}
void CAbiEngineClient::SelectCamera(int device_index) {
  Submit(QStringLiteral("camera.select"),
         QJsonObject{{QStringLiteral("device_index"), device_index}});
}
void CAbiEngineClient::SelectRecordingDevice(
    const std::string& device_id) {
  Submit(QStringLiteral("media.selectRecordingDevice"),
         QJsonObject{{QStringLiteral("device_id"),
                      QString::fromStdString(device_id)}});
}
void CAbiEngineClient::SelectPlayoutDevice(
    const std::string& device_id) {
  Submit(QStringLiteral("media.selectPlayoutDevice"),
         QJsonObject{{QStringLiteral("device_id"),
                      QString::fromStdString(device_id)}});
}
void CAbiEngineClient::SelectSystemAudioDevice(
    const std::string& device_id) {
  Submit(QStringLiteral("media.selectSystemAudioDevice"),
         QJsonObject{{QStringLiteral("device_id"),
                      QString::fromStdString(device_id)}});
}
void CAbiEngineClient::SelectScreenSource(const std::string& source_id,
                                          bool window) {
  Submit(QStringLiteral("media.selectScreenSource"),
         QJsonObject{{QStringLiteral("source_id"),
                      QString::fromStdString(source_id)},
                     {QStringLiteral("window"), window}});
}
void CAbiEngineClient::StartCameraPreview(int device_index) {
  Submit(QStringLiteral("camera.preview.start"),
         QJsonObject{{QStringLiteral("device_index"), device_index}});
}
void CAbiEngineClient::StopCameraPreview() {
  Submit(QStringLiteral("camera.preview.stop"));
}

void CAbiEngineClient::OnEvent(void* userdata, const char* method,
                               const char* json) {
  auto* context = static_cast<CallbackContext*>(userdata);
  if (context == nullptr || context->self == nullptr || method == nullptr ||
      json == nullptr) {
    return;
  }
  const QString method_text = QString::fromUtf8(method);
  const QByteArray body(json);
  CAbiEngineClient* self = context->self;
  QMetaObject::invokeMethod(
      self,
      [self, method_text, body] {
        self->HandleEvent(method_text,
                          QJsonDocument::fromJson(body).object());
      },
      Qt::QueuedConnection);
}

void CAbiEngineClient::OnRemoteFrame(void* userdata,
                                     const rtcwin_frame_view* frame) {
  auto* context = static_cast<CallbackContext*>(userdata);
  if (context == nullptr || context->self == nullptr || frame == nullptr) return;
  domain::VideoFrame copied = CopyFrame(*frame);
  if (copied.width <= 0 || copied.y.empty()) return;
  if (copied.uid.empty()) return;

  CAbiEngineClient* self = context->self;
  const std::string key =
      copied.uid + "|" + domain::VideoSourceName(copied.source);
  {
    std::lock_guard<std::mutex> lock(self->remote_frame_mutex_);
    self->latest_remote_frames_[key] = std::move(copied);
  }
  if (self->remote_frame_post_pending_.exchange(true)) return;
  QMetaObject::invokeMethod(
      self, [self] { self->DrainLatestRemoteFrames(); }, Qt::QueuedConnection);
}

void CAbiEngineClient::OnLocalFrame(void* userdata,
                                    const rtcwin_frame_view* frame) {
  auto* context = static_cast<CallbackContext*>(userdata);
  if (context == nullptr || context->self == nullptr || frame == nullptr) return;
  domain::VideoFrame copied = CopyFrame(*frame);
  if (copied.width <= 0 || copied.y.empty()) return;

  CAbiEngineClient* self = context->self;
  const std::string stream_uid =
      copied.uid.empty() ? "local" : copied.uid;
  const std::string key =
      stream_uid + "|" + domain::VideoSourceName(copied.source);
  {
    std::lock_guard<std::mutex> lock(self->local_frame_mutex_);
    self->latest_local_frames_[key] = std::move(copied);
  }
  if (self->local_frame_post_pending_.exchange(true)) return;
  QMetaObject::invokeMethod(
      self, [self] { self->DrainLatestLocalFrames(); }, Qt::QueuedConnection);
}

void CAbiEngineClient::DrainLatestLocalFrames() {
  std::map<std::string, domain::VideoFrame> frames;
  {
    std::lock_guard<std::mutex> lock(local_frame_mutex_);
    frames.swap(latest_local_frames_);
  }
  local_frame_post_pending_.store(false);
  for (const auto& item : frames) {
    emit LocalFrameReceived(item.second);
  }

  bool has_newer_frame = false;
  {
    std::lock_guard<std::mutex> lock(local_frame_mutex_);
    has_newer_frame = !latest_local_frames_.empty();
  }
  if (has_newer_frame && !local_frame_post_pending_.exchange(true)) {
    QMetaObject::invokeMethod(
        this, [this] { DrainLatestLocalFrames(); }, Qt::QueuedConnection);
  }
}

void CAbiEngineClient::DrainLatestRemoteFrames() {
  std::map<std::string, domain::VideoFrame> frames;
  {
    std::lock_guard<std::mutex> lock(remote_frame_mutex_);
    frames.swap(latest_remote_frames_);
  }
  remote_frame_post_pending_.store(false);
  for (const auto& item : frames) {
    emit RemoteFrameReceived(item.second);
  }

  bool has_newer_frames = false;
  {
    std::lock_guard<std::mutex> lock(remote_frame_mutex_);
    has_newer_frames = !latest_remote_frames_.empty();
  }
  if (has_newer_frames && !remote_frame_post_pending_.exchange(true)) {
    QMetaObject::invokeMethod(
        this, [this] { DrainLatestRemoteFrames(); }, Qt::QueuedConnection);
  }
}

uint64_t CAbiEngineClient::Submit(const QString& method,
                                  const QJsonObject& payload) {
  if (handle_ == nullptr) {
    domain::AppError error;
    error.domain = domain::ErrorDomain::Internal;
    error.code = 1902;
    error.message = "引擎句柄创建失败";
    emit ErrorOccurred(error);
    return 0;
  }
  const QByteArray json =
      QJsonDocument(payload).toJson(QJsonDocument::Compact);
  const QByteArray method_text = method.toUtf8();
  if (EventTraceEnabled()) {
    qInfo().noquote() << "[rtcwin:submit]" << method
                      << QJsonDocument(TraceObject(payload)).toJson(
                             QJsonDocument::Compact);
  }
  const uint64_t id = rtcwin_submit(handle_, method_text.constData(),
                                    json.constData(), 15000);
  if (id == 0) {
    domain::AppError error;
    error.domain = domain::ErrorDomain::Internal;
    error.code = 1903;
    error.message = "引擎命令提交失败";
    emit ErrorOccurred(error);
  }
  return id;
}

void CAbiEngineClient::HandleEvent(const QString& method,
                                   const QJsonObject& event) {
  if (EventTraceEnabled()) {
    qInfo().noquote() << "[rtcwin:event]"
                      << QJsonDocument(TraceObject(event)).toJson(
                             QJsonDocument::Compact);
  }
  const QString type = event.value(QStringLiteral("type")).toString();
  if (type == QStringLiteral("ntf")) {
    if (method == QStringLiteral("camera.preview.error")) {
      emit CameraPreviewChanged(false, {}, 0, 0, 0);
      domain::AppError error;
      error.domain = domain::ErrorDomain::Device;
      error.code = 1502;
      error.operation = "camera.preview";
      error.message = event.value(QStringLiteral("data"))
                          .toObject()
                          .value(QStringLiteral("message"))
                          .toString()
                          .toStdString();
      emit ErrorOccurred(error);
    } else if (method == QStringLiteral("session.error")) {
      const QJsonObject data =
          event.value(QStringLiteral("data")).toObject();
      domain::AppError error;
      error.domain = domain::ErrorDomain::Network;
      error.code = data.value(QStringLiteral("code")).toInt(480);
      error.operation = "signaling";
      error.message =
          data.value(QStringLiteral("message")).toString().toStdString();
      emit ErrorOccurred(error);
    } else if (method == QStringLiteral("session.ready")) {
      emit Notice(QStringLiteral("信令连接已就绪"), QStringLiteral("ok"));
    } else if (method == QStringLiteral("diagnostics.log_path")) {
      const QString path = event.value(QStringLiteral("data"))
                               .toObject()
                               .value(QStringLiteral("path"))
                               .toString();
      if (!path.isEmpty()) {
        emit Notice(QStringLiteral("诊断日志：%1").arg(path),
                    QStringLiteral("info"));
      }
    } else if (method == QStringLiteral("media.ready")) {
      emit Notice(QStringLiteral("音频设备已就绪"), QStringLiteral("ok"));
    } else if (method == QStringLiteral("media.published")) {
      emit Notice(QStringLiteral("音频通道已发布"), QStringLiteral("ok"));
    } else if (method == QStringLiteral("media.muted") ||
               method == QStringLiteral("media.deafened")) {
      const QJsonObject data =
          event.value(QStringLiteral("data")).toObject();
      if (method == QStringLiteral("media.muted")) {
        snapshot_.mic_muted =
            data.value(QStringLiteral("muted")).toBool(snapshot_.mic_muted);
      } else {
        snapshot_.deafened = data.value(QStringLiteral("deafened"))
                                 .toBool(snapshot_.deafened);
      }
      emit SnapshotChanged(snapshot_);
    } else if (method == QStringLiteral("video.state")) {
      const QJsonObject data =
          event.value(QStringLiteral("data")).toObject();
      const QString source = data.value(QStringLiteral("source")).toString();
      const bool sharing = data.value(QStringLiteral("sharing")).toBool(false);
      if (source == QStringLiteral("camera")) {
        snapshot_.camera_on = sharing;
      } else if (source == QStringLiteral("screen")) {
        snapshot_.sharing = sharing;
      }
      emit SnapshotChanged(snapshot_);
    } else if (method == QStringLiteral("media.producer")) {
      const QJsonObject data =
          event.value(QStringLiteral("data")).toObject();
      const std::string uid =
          data.value(QStringLiteral("uid")).toString().toStdString();
      const QString source = data.value(QStringLiteral("source")).toString();
      const bool paused = data.value(QStringLiteral("paused")).toBool(true);
      const bool on = !paused;
      if (uid == snapshot_.self_uid) {
        if (source == QStringLiteral("camera")) {
          snapshot_.camera_on = on;
        } else if (source == QStringLiteral("screen")) {
          snapshot_.sharing = on;
        }
      } else {
        for (domain::PeerState& peer : snapshot_.peers) {
          if (peer.uid != uid) continue;
          if (source == QStringLiteral("camera")) {
            peer.camera_on = on;
          } else if (source == QStringLiteral("screen")) {
            peer.screen_on = on;
          }
          break;
        }
      }
      const domain::VideoSource video_source =
          source == QStringLiteral("screen") ? domain::VideoSource::Screen
                                             : domain::VideoSource::Camera;
      for (domain::VideoStreamState& stream : snapshot_.streams) {
        if (stream.uid == uid && stream.source == video_source) {
          stream.producer_paused = paused;
          stream.receiving = on;
        }
      }
      emit SnapshotChanged(snapshot_);
    } else if (method == QStringLiteral("media.track")) {
      const QJsonObject data =
          event.value(QStringLiteral("data")).toObject();
      if (data.value(QStringLiteral("kind")).toString() !=
          QStringLiteral("video")) {
        return;
      }
      const std::string mid =
          data.value(QStringLiteral("mid")).toString().toStdString();
      if (!mid.empty()) {
        domain::VideoStreamState* stream = nullptr;
        for (domain::VideoStreamState& current : snapshot_.streams) {
          if (current.mid == mid) {
            stream = &current;
            break;
          }
        }
        if (stream == nullptr) {
          snapshot_.streams.push_back({});
          stream = &snapshot_.streams.back();
          stream->mid = mid;
        }
        const QString uid = data.value(QStringLiteral("uid")).toString();
        if (!uid.isEmpty()) stream->uid = uid.toStdString();
        stream->source =
            data.value(QStringLiteral("source")).toString() ==
                    QStringLiteral("screen")
                ? domain::VideoSource::Screen
                : domain::VideoSource::Camera;
        stream->producer_paused = false;
        stream->receiving = false;
        emit SnapshotChanged(snapshot_);
      }
    } else if (method == QStringLiteral("media.error")) {
      const QJsonObject data =
          event.value(QStringLiteral("data")).toObject();
      domain::AppError error;
      error.domain = domain::ErrorDomain::Media;
      error.code = data.value(QStringLiteral("code")).toInt(1700);
      error.operation = "media";
      error.message = data.value(QStringLiteral("message"))
                          .toString(QStringLiteral("音频媒体错误"))
                          .toStdString();
      error.detail =
          data.value(QStringLiteral("detail")).toString().toStdString();
      emit ErrorOccurred(error);
    } else if (method.startsWith(QStringLiteral("room.")) ||
               method == QStringLiteral("floor.state") ||
               method == QStringLiteral("floor.local") ||
               method == QStringLiteral("floor.revoked") ||
               method == QStringLiteral("session.kicked") ||
               method == QStringLiteral("session.closed")) {
      ApplyRoomNotification(
          method, event.value(QStringLiteral("data")).toObject());
    }
    return;
  }
  if (type != QStringLiteral("res")) return;
  const bool ok = event.value(QStringLiteral("ok")).toBool(false);
  if (ok) {
    const QJsonObject data = event.value(QStringLiteral("data")).toObject();
    if (method == QStringLiteral("camera.devices")) {
      QVector<domain::CameraDevice> devices;
      for (const QJsonValue& value :
           data.value(QStringLiteral("cameras")).toArray()) {
        const QJsonObject object = value.toObject();
        domain::CameraDevice device;
        device.id = object.value(QStringLiteral("id")).toString().toStdString();
        device.name =
            object.value(QStringLiteral("name")).toString().toStdString();
        if (!device.name.empty()) devices.push_back(device);
      }
      emit CameraDevicesChanged(devices);
    } else if (method == QStringLiteral("audio.devices")) {
      const auto parse_devices = [](const QJsonArray& array) {
        QVector<domain::AudioDevice> devices;
        for (const QJsonValue& value : array) {
          const QJsonObject object = value.toObject();
          domain::AudioDevice device;
          device.id =
              object.value(QStringLiteral("id")).toString().toStdString();
          device.name =
              object.value(QStringLiteral("name")).toString().toStdString();
          device.is_default = devices.isEmpty();
          device.available = !device.name.empty();
          if (device.available) devices.push_back(device);
        }
        return devices;
      };
      emit AudioDevicesChanged(
          parse_devices(data.value(QStringLiteral("recording")).toArray()),
          parse_devices(data.value(QStringLiteral("playout")).toArray()));
    } else if (method == QStringLiteral("desktop.sources")) {
      QVector<domain::DesktopSource> sources;
      for (const QJsonValue& value :
           data.value(QStringLiteral("sources")).toArray()) {
        const QJsonObject object = value.toObject();
        domain::DesktopSource source;
        source.id =
            object.value(QStringLiteral("id")).toString().toStdString();
        source.title =
            object.value(QStringLiteral("title")).toString().toStdString();
        source.window =
            object.value(QStringLiteral("type")).toString() ==
            QStringLiteral("window");
        if (!source.id.empty()) sources.push_back(source);
      }
      emit DesktopSourcesChanged(sources);
    } else if (method == QStringLiteral("camera.preview.start")) {
      emit CameraPreviewChanged(
          true, data.value(QStringLiteral("device")).toString(),
          data.value(QStringLiteral("width")).toInt(),
          data.value(QStringLiteral("height")).toInt(),
          data.value(QStringLiteral("fps")).toInt());
    } else if (method == QStringLiteral("camera.preview.stop")) {
      emit CameraPreviewChanged(false, {}, 0, 0, 0);
    } else if (method == QStringLiteral("session.connectAndJoin")) {
      ApplyJoinData(data);
      emit Joined();
    } else if (method == QStringLiteral("host.setPolicy")) {
      ApplyPolicy(data.value(QStringLiteral("policy")).toObject(),
                  &snapshot_.policy);
      snapshot_.talk_mode = snapshot_.policy.talk_mode;
      emit SnapshotChanged(snapshot_);
    } else if (method == QStringLiteral("room.leave")) {
      snapshot_ = {};
      emit SnapshotChanged(snapshot_);
      emit Left();
    } else if (method == QStringLiteral("floor.request")) {
      const bool granted =
          data.value(QStringLiteral("granted")).toBool(false);
      if (granted) {
        snapshot_.floor.self_has_floor = true;
        snapshot_.floor.holder_uid = snapshot_.self_uid;
        snapshot_.mic_muted = false;
      } else if (data.contains(QStringLiteral("queue_pos"))) {
        const int position =
            data.value(QStringLiteral("queue_pos")).toInt(0);
        if (position > 0 &&
            std::find(snapshot_.floor.queue.begin(),
                      snapshot_.floor.queue.end(),
                      snapshot_.self_uid) ==
                snapshot_.floor.queue.end()) {
          snapshot_.floor.queue.push_back(snapshot_.self_uid);
        }
      }
      emit SnapshotChanged(snapshot_);
    } else if (method == QStringLiteral("floor.release")) {
      snapshot_.floor.self_has_floor = false;
      if (snapshot_.floor.holder_uid == snapshot_.self_uid) {
        snapshot_.floor.holder_uid.clear();
      }
      snapshot_.mic_muted = true;
      emit SnapshotChanged(snapshot_);
    } else if (method == QStringLiteral("session.disconnect")) {
      snapshot_ = {};
      emit SnapshotChanged(snapshot_);
      emit Left();
    }
    return;
  }
  if (method == QStringLiteral("camera.preview.start")) {
    emit CameraPreviewChanged(false, {}, 0, 0, 0);
  }
  const QJsonObject detail = event.value(QStringLiteral("error")).toObject();
  domain::AppError error;
  error.domain = domain::ErrorDomain::Internal;
  error.code = detail.value(QStringLiteral("code")).toInt(500);
  error.operation = method.toStdString();
  error.message =
      detail.value(QStringLiteral("msg")).toString().toStdString();
  error.detail = "C ABI engine";
  emit ErrorOccurred(error);
}

}  // namespace rtcwin::client_gateway
