#include "rtcwin/client_gateway/c_abi_engine_client.h"

#include "rtcwin/client_api.h"

#include <QByteArray>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonValue>
#include <QMetaObject>

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
    } else if (method == QStringLiteral("camera.preview.start")) {
      emit CameraPreviewChanged(
          true, data.value(QStringLiteral("device")).toString(),
          data.value(QStringLiteral("width")).toInt(),
          data.value(QStringLiteral("height")).toInt(),
          data.value(QStringLiteral("fps")).toInt());
    } else if (method == QStringLiteral("camera.preview.stop")) {
      emit CameraPreviewChanged(false, {}, 0, 0, 0);
    } else if (method == QStringLiteral("room.leave")) {
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
