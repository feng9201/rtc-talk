#include "rtcwin/client_gateway/c_abi_engine_client.h"

#include "rtcwin/client_api.h"

#include <QByteArray>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QMetaObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>
#include <QUrlQuery>

#include <cstring>
#include <utility>

namespace rtcwin::client_gateway {
namespace {

domain::VideoFrame CopyFrame(const rtcwin_frame_view& view) {
  domain::VideoFrame frame;
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
  snapshot_.media_enabled = false;
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
  if (!policy.isEmpty()) {
    snapshot_.policy.talk_mode = ParseTalkMode(
        policy.value(QStringLiteral("talk_mode")).toString());
    snapshot_.policy.max_speakers =
        policy.value(QStringLiteral("max_speakers")).toInt();
    snapshot_.policy.ptt_max_hold_seconds =
        policy.value(QStringLiteral("ptt_max_hold_sec"))
            .toInt(snapshot_.policy.ptt_max_hold_seconds);
    snapshot_.policy.ptt_queue =
        policy.value(QStringLiteral("ptt_queue"))
            .toBool(snapshot_.policy.ptt_queue);
    snapshot_.policy.max_peers =
        policy.value(QStringLiteral("max_peers"))
            .toInt(snapshot_.policy.max_peers);
    snapshot_.policy.video_enabled =
        policy.value(QStringLiteral("video_enabled"))
            .toBool(snapshot_.policy.video_enabled);
    snapshot_.policy.video_max_publishers =
        policy.value(QStringLiteral("video_max_publishers"))
            .toInt(snapshot_.policy.video_max_publishers);
  }
  snapshot_.talk_mode = snapshot_.policy.talk_mode;
  const QJsonObject floor = data.value(QStringLiteral("floor")).toObject();
  snapshot_.floor.holder_uid =
      floor.value(QStringLiteral("holder")).toString().toStdString();
  snapshot_.floor.queue.clear();
  for (const QJsonValue& value :
       floor.value(QStringLiteral("queue")).toArray()) {
    snapshot_.floor.queue.push_back(value.toString().toStdString());
  }
  snapshot_.floor.self_has_floor =
      snapshot_.floor.holder_uid == snapshot_.self_uid;
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
    snapshot_.policy.talk_mode = ParseTalkMode(
        data.value(QStringLiteral("talk_mode")).toString());
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
    snapshot_.floor.self_has_floor =
        snapshot_.floor.holder_uid == snapshot_.self_uid;
  } else if (method == QStringLiteral("room.activeSpeaker")) {
    const std::string uid =
        data.value(QStringLiteral("uid")).toString().toStdString();
    for (domain::PeerState& peer : snapshot_.peers) {
      peer.speaking = peer.uid == uid;
    }
  } else if (method == QStringLiteral("room.audioLevels")) {
    for (const QJsonValue& value :
         data.value(QStringLiteral("levels")).toArray()) {
      const QJsonObject level = value.toObject();
      const std::string uid =
          level.value(QStringLiteral("uid")).toString().toStdString();
      for (domain::PeerState& peer : snapshot_.peers) {
        if (peer.uid == uid) {
          peer.audio_level =
              level.value(QStringLiteral("level")).toDouble();
          break;
        }
      }
    }
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
void CAbiEngineClient::ToggleDeafened() {
  Submit(QStringLiteral("media.toggleDeafened"));
}
void CAbiEngineClient::RequestFloor() {
  Submit(QStringLiteral("floor.request"));
}
void CAbiEngineClient::ReleaseFloor() {
  Submit(QStringLiteral("floor.release"));
}
void CAbiEngineClient::SelectPeer(const std::string& uid) { Q_UNUSED(uid); }
void CAbiEngineClient::RefreshCameras() {
  Submit(QStringLiteral("camera.devices"));
}
void CAbiEngineClient::RefreshAudioDevices() {
  Submit(QStringLiteral("audio.devices"));
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
  Q_UNUSED(userdata);
  Q_UNUSED(frame);
}

void CAbiEngineClient::OnLocalFrame(void* userdata,
                                    const rtcwin_frame_view* frame) {
  auto* context = static_cast<CallbackContext*>(userdata);
  if (context == nullptr || context->self == nullptr || frame == nullptr) return;
  domain::VideoFrame copied = CopyFrame(*frame);
  if (copied.width <= 0 || copied.y.empty()) return;

  CAbiEngineClient* self = context->self;
  auto latest = std::make_shared<domain::VideoFrame>(std::move(copied));
  {
    std::lock_guard<std::mutex> lock(self->local_frame_mutex_);
    self->latest_local_frame_ = std::move(latest);
  }
  if (self->local_frame_post_pending_.exchange(true)) return;
  QMetaObject::invokeMethod(
      self, [self] { self->DrainLatestLocalFrame(); }, Qt::QueuedConnection);
}

void CAbiEngineClient::DrainLatestLocalFrame() {
  std::shared_ptr<domain::VideoFrame> frame;
  {
    std::lock_guard<std::mutex> lock(local_frame_mutex_);
    frame = std::move(latest_local_frame_);
  }
  local_frame_post_pending_.store(false);
  if (frame) emit LocalFrameReceived(*frame);

  bool has_newer_frame = false;
  {
    std::lock_guard<std::mutex> lock(local_frame_mutex_);
    has_newer_frame = latest_local_frame_ != nullptr;
  }
  if (has_newer_frame && !local_frame_post_pending_.exchange(true)) {
    QMetaObject::invokeMethod(
        this, [this] { DrainLatestLocalFrame(); }, Qt::QueuedConnection);
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
    } else if (method.startsWith(QStringLiteral("room.")) ||
               method == QStringLiteral("floor.state") ||
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
    } else if (method == QStringLiteral("room.leave")) {
      snapshot_ = {};
      emit SnapshotChanged(snapshot_);
      emit Left();
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
