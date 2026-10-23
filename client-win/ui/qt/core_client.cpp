// File: ui/qt/core_client.cpp
// Brief: 实现核心库句柄管理与事件线程切换。
#include "core_client.h"

#include <QByteArray>
#include <QJsonDocument>
#include <QMetaObject>

CoreClient::CoreClient(QObject* parent) : QObject(parent) {}

CoreClient::~CoreClient() {
  if (core_ != nullptr) {
    talk_destroy(core_);
    core_ = nullptr;
  }
}

bool CoreClient::Start(const QJsonObject& config) {
  ++generation_;
  if (core_ != nullptr) {
    talk_destroy(core_);
    core_ = nullptr;
  }
  context_ = std::make_unique<Context>(Context{this, generation_});
  const QByteArray text =
      QJsonDocument(config).toJson(QJsonDocument::Compact);
  talk_options options{};
  options.struct_size = sizeof(talk_options);
  options.abi_version = TALK_ABI_VERSION;
  options.on_event = &CoreClient::Dispatch;
  options.userdata = context_.get();
  options.config_json = text.constData();
  core_ = talk_create(&options);
  return core_ != nullptr;
}

bool CoreClient::Submit(const QString& method, const QJsonObject& data) {
  if (core_ == nullptr) {
    return false;
  }
  const QByteArray text =
      QJsonDocument(data).toJson(QJsonDocument::Compact);
  const QByteArray name = method.toUtf8();
  return talk_command(core_, name.constData(), text.constData()) != 0;
}

void CoreClient::SetVideoCallback(bool on) {
  if (core_ == nullptr) {
    return;
  }
  talk_set_video_frame_callback(core_,
                                on ? &CoreClient::DispatchVideo : nullptr,
                                on ? context_.get() : nullptr);
}

void CoreClient::Dispatch(void* context, const char* name, const char* text) {
  // 回调线程只允许拷贝上下文，跨线程使用 Qt 队列投递。
  const Context state = *static_cast<Context*>(context);
  const QString event_name = QString::fromUtf8(name);
  const QJsonObject data =
      QJsonDocument::fromJson(QByteArray(text)).object();
  QMetaObject::invokeMethod(
      state.self,
      [state, event_name, data] {
        if (state.generation == state.self->generation_) {
          state.self->Deliver(event_name, data);
        }
      },
      Qt::QueuedConnection);
}

void CoreClient::Deliver(const QString& name, const QJsonObject& data) {
  emit Event(name, data);
}

void CoreClient::DispatchVideo(void* context, const char* mid,
                               const uint8_t* bgra, int width, int height,
                               int stride, int64_t timestamp_us) {
  Q_UNUSED(timestamp_us);
  if (context == nullptr || bgra == nullptr || width <= 0 || height <= 0 ||
      stride < width * 4) {
    return;
  }
  // 媒体线程：先把裸指针拷成独立 QImage，再切到 UI 线程。
  const Context state = *static_cast<Context*>(context);
  const QImage view(bgra, width, height, stride, QImage::Format_ARGB32);
  const QImage frame = view.copy();
  const QString mid_text = QString::fromUtf8(mid != nullptr ? mid : "");
  QMetaObject::invokeMethod(
      state.self,
      [state, frame, mid_text] {
        if (state.generation == state.self->generation_) {
          state.self->DeliverVideo(frame, mid_text);
        }
      },
      Qt::QueuedConnection);
}

void CoreClient::DeliverVideo(const QImage& frame, const QString& mid) {
  emit VideoFrame(frame, mid);
}
