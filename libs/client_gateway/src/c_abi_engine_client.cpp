#include "rtcwin/client_gateway/c_abi_engine_client.h"

#include "rtcwin/client_api.h"

#include <QByteArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QMetaObject>

namespace rtcwin::client_gateway {

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
  QJsonObject payload;
  payload.insert(QStringLiteral("url"),
                 QString::fromStdString(options.signaling_url));
  payload.insert(QStringLiteral("user_id"),
                 QString::fromStdString(options.user_id));
  payload.insert(QStringLiteral("room_id"),
                 QString::fromStdString(options.room_id));
  payload.insert(QStringLiteral("token"),
                 QString::fromStdString(options.token));
  payload.insert(QStringLiteral("fetch_dev_token"), options.fetch_dev_token);
  payload.insert(QStringLiteral("volume"), options.volume);
  Submit(QStringLiteral("session.connectAndJoin"), payload);
}

void CAbiEngineClient::LeaveRoom() {
  Submit(QStringLiteral("room.leave"));
}

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

void CAbiEngineClient::SelectPeer(const std::string& uid) {
  Q_UNUSED(uid);
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
        const QJsonObject event = QJsonDocument::fromJson(body).object();
        self->HandleEvent(method_text, event);
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
  Q_UNUSED(userdata);
  Q_UNUSED(frame);
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
  if (event.value(QStringLiteral("type")).toString() !=
      QStringLiteral("res")) {
    return;
  }
  const bool ok = event.value(QStringLiteral("ok")).toBool(false);
  if (ok) {
    if (method == QStringLiteral("room.leave")) emit Left();
    return;
  }
  const QJsonObject detail =
      event.value(QStringLiteral("error")).toObject();
  domain::AppError error;
  error.domain = domain::ErrorDomain::Internal;
  error.code = detail.value(QStringLiteral("code")).toInt(500);
  error.operation = method.toStdString();
  error.message =
      detail.value(QStringLiteral("msg")).toString().toStdString();
  error.detail = "C ABI engine";
  error.action = domain::RecoveryAction::None;
  emit ErrorOccurred(error);
}

}  // namespace rtcwin::client_gateway
