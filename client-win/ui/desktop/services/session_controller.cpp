#include "services/session_controller.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>
#include <QUrlQuery>

#include <variant>

namespace talk::desktop {
namespace {

QString AudioBackendValue(AudioBackend backend) {
  switch (backend) {
    case AudioBackend::None:
      return QStringLiteral("none");
    case AudioBackend::Fake:
      return QStringLiteral("fake");
    case AudioBackend::Wasapi:
      return QStringLiteral("wasapi");
  }
  return QStringLiteral("wasapi");
}

QString AudioSourceValue(AudioSource source) {
  return source == AudioSource::System ? QStringLiteral("system")
                                       : QStringLiteral("mic");
}

QString MediaModeValue(MediaModePreference mode) {
  switch (mode) {
    case MediaModePreference::Sfu:
      return QStringLiteral("sfu");
    case MediaModePreference::P2p:
      return QStringLiteral("p2p");
    case MediaModePreference::Auto:
      return QStringLiteral("auto");
  }
  return QStringLiteral("auto");
}

QString TalkModeValue(TalkMode mode) {
  switch (mode) {
    case TalkMode::Ptt:
      return QStringLiteral("ptt");
    case TalkMode::Moderated:
      return QStringLiteral("moderated");
    case TalkMode::Free:
      return QStringLiteral("free");
  }
  return QStringLiteral("free");
}

}  // namespace

SessionController::SessionController(QObject* parent)
    : QObject(parent), network_(new QNetworkAccessManager(this)) {
  connect(&core_, &TalkCoreClient::CoreEventReceived, this,
          &SessionController::HandleCoreEvent);
  connect(&core_, &TalkCoreClient::CommandCompleted, this,
          &SessionController::HandleCommandCompleted);
  connect(&core_, &TalkCoreClient::CommandTimedOut, this,
          &SessionController::HandleCommandTimedOut);
  connect(&store_, &SessionStore::SnapshotChanged, this,
          [this] { emit StateChanged(store_.snapshot()); });
  connect(&store_, &SessionStore::Joined, this, [this] {
    joining_ = false;
    SetBusy(false, QStringLiteral("已进入房间"));
    emit Joined();
  });
  connect(&store_, &SessionStore::Left, this, [this] {
    joining_ = false;
    SetBusy(false, QStringLiteral("已离开房间"));
    emit Left();
    QTimer::singleShot(0, this, [this] {
      core_.Stop();
      store_.Reset();
    });
  });
  connect(&store_, &SessionStore::Kicked, this,
          &SessionController::HandleKicked);
  connect(&store_, &SessionStore::ErrorOccurred, this,
          &SessionController::HandleStoreError);
}

void SessionController::ConnectAndJoin(const PrejoinOptions& options) {
  if (busy_) return;
  options_ = options;
  options_.token = options.token.trimmed();
  ResetSession();

  const QUrl endpoint(options_.url);
  if (endpoint.scheme() != QStringLiteral("ws") || endpoint.host().isEmpty() ||
      endpoint.hasQuery()) {
    Fail(QStringLiteral("信令地址无效"),
         QStringLiteral("当前核心仅支持不含查询参数的 ws:// 地址"));
    return;
  }
  if (options_.userId.trimmed().isEmpty() ||
      options_.roomId.trimmed().isEmpty()) {
    Fail(QStringLiteral("请填写用户标识和房间 ID"));
    return;
  }
  if (!options_.fetchDevToken && options_.token.isEmpty()) {
    Fail(QStringLiteral("请填写 Token 或启用开发测试模式"));
    return;
  }
  if (!StartCore(options_)) {
    Fail(QStringLiteral("创建媒体核心失败"),
         QStringLiteral("请检查音频后端和日志"));
    return;
  }

  store_.SetLocalIdentity(options_.userId, options_.userId);
  if (!options_.fetchDevToken) {
    SubmitConnect(options_.token);
    return;
  }

  SetBusy(true, QStringLiteral("正在获取开发凭证…"));
  QUrl token_endpoint = endpoint;
  token_endpoint.setScheme(QStringLiteral("http"));
  token_endpoint.setPath(QStringLiteral("/dev/token"));
  QUrlQuery query;
  query.addQueryItem(QStringLiteral("uid"), options_.userId);
  query.addQueryItem(QStringLiteral("rooms"), options_.roomId);
  token_endpoint.setQuery(query);

  QNetworkRequest request(token_endpoint);
  QNetworkReply* reply = network_->get(request);
  QTimer::singleShot(8000, reply, [reply] {
    if (reply->isRunning()) reply->abort();
  });
  connect(reply, &QNetworkReply::finished, this, [this, reply] {
    const QByteArray body = reply->readAll();
    const QJsonObject data = QJsonDocument::fromJson(body).object();
    const QString token = data.value(QStringLiteral("token")).toString();
    const QString network_error = reply->errorString();
    const bool network_ok = reply->error() == QNetworkReply::NoError;
    reply->deleteLater();

    if (!network_ok || token.isEmpty()) {
      Fail(QStringLiteral("开发凭证获取失败"),
           network_ok ? QStringLiteral("服务端未返回 token") : network_error);
      return;
    }
    options_.token = token;
    SubmitConnect(token);
  });
}

void SessionController::LeaveRoom() {
  if (busy_ || !store_.snapshot().joined) return;
  SetBusy(true, QStringLiteral("正在离开房间…"));
  const quint64 id = core_.Submit(QStringLiteral("room.leave"));
  if (id == 0) {
    SetBusy(false, QString());
    Fail(QStringLiteral("离房命令提交失败"));
  }
}

void SessionController::Disconnect() {
  if (core_.IsRunning()) {
    core_.Submit(QStringLiteral("disconnect"), QJsonObject(), 3000);
    core_.Stop();
  }
  ResetSession();
  SetBusy(false, QStringLiteral("已断开连接"));
}

void SessionController::HandleCoreEvent(const CoreEvent& event) {
  store_.ApplyEvent(event);

  if (const auto* ready = std::get_if<ReadyEvent>(&event.payload)) {
    if (!ready->uid.isEmpty()) {
      store_.SetLocalIdentity(ready->uid, options_.userId);
    }
    if (!ready->resumed || !store_.snapshot().joined) SubmitJoin();
  } else if (std::get_if<JoinedEvent>(&event.payload) != nullptr) {
    joining_ = false;
  } else if (const auto* state = std::get_if<StateEvent>(&event.payload)) {
    if (state->state == ConnectionState::Disconnected) {
      joining_ = false;
      SetBusy(false, QString());
      QTimer::singleShot(0, this, &SessionController::ResetSession);
    }
  }
}

void SessionController::HandleCommandCompleted(const CommandResult& result) {
  if (result.ok) return;
  if (result.method == QStringLiteral("connect") ||
      result.method == QStringLiteral("room.join") ||
      result.method == QStringLiteral("room.leave")) {
    joining_ = false;
    SetBusy(false, QString());
  }
}

void SessionController::HandleCommandTimedOut(quint64 command_id,
                                              const QString& method) {
  Q_UNUSED(command_id);
  joining_ = false;
  SetBusy(false, QString());
  Fail(QStringLiteral("操作超时"), method);
}

void SessionController::HandleStoreError(int code, const QString& message) {
  busy_ = false;
  emit BusyChanged(false, message);
  emit ErrorOccurred(message, code > 0 ? QString::number(code) : QString());
  if (!store_.snapshot().joined) {
    core_.Stop();
    store_.Reset();
  }
}

void SessionController::HandleKicked(const QString& reason) {
  joining_ = false;
  SetBusy(false, QString());
  emit Kicked(reason);
  QTimer::singleShot(0, this, [this] { core_.Stop(); });
}

void SessionController::SubmitConnect(const QString& token) {
  SetBusy(true, QStringLiteral("正在连接信令服务…"));
  const quint64 id = core_.Submit(
      QStringLiteral("connect"),
      QJsonObject{{QStringLiteral("url"), options_.url},
                  {QStringLiteral("token"), token}});
  if (id == 0) {
    SetBusy(false, QString());
    Fail(QStringLiteral("连接命令提交失败"));
  }
}

void SessionController::SubmitJoin() {
  if (joining_ || options_.roomId.isEmpty()) return;
  joining_ = true;
  SetBusy(true, QStringLiteral("正在加入房间…"));
  const quint64 id = core_.Submit(
      QStringLiteral("room.join"),
      QJsonObject{{QStringLiteral("room_id"), options_.roomId},
                  {QStringLiteral("display"), options_.userId}});
  if (id == 0) {
    joining_ = false;
    SetBusy(false, QString());
    Fail(QStringLiteral("入房命令提交失败"));
  }
}

bool SessionController::StartCore(const PrejoinOptions& options) {
  const QJsonObject config{
      {QStringLiteral("audio_backend"), AudioBackendValue(options.audioBackend)},
      {QStringLiteral("audio_source"), AudioSourceValue(options.audioSource)},
      {QStringLiteral("media_mode"), MediaModeValue(options.mediaMode)},
      {QStringLiteral("talk_mode"), TalkModeValue(options.talkMode)},
      {QStringLiteral("recording_device"), options.recordingDevice},
      {QStringLiteral("playout_device"), options.playoutDevice},
      {QStringLiteral("camera_device"), options.cameraDevice}};
  return core_.Start(config);
}

void SessionController::SetBusy(bool busy, const QString& message) {
  if (busy_ == busy && message.isEmpty()) return;
  busy_ = busy;
  emit BusyChanged(busy, message);
}

void SessionController::Fail(const QString& message, const QString& detail) {
  joining_ = false;
  core_.Stop();
  store_.Reset();
  SetBusy(false, message);
  emit ErrorOccurred(message, detail);
}

void SessionController::ResetSession() {
  core_.Stop();
  store_.Reset();
}

}  // namespace talk::desktop
