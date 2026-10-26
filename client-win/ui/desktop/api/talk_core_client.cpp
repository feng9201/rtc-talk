#include "api/talk_core_client.h"

#include "api/event_mapper.h"

#include <QByteArray>
#include <QJsonDocument>
#include <QMetaObject>

namespace talk::desktop {

TalkCoreClient::TalkCoreClient(QObject* parent) : QObject(parent) {
  qRegisterMetaType<CoreEvent>();
  qRegisterMetaType<CommandResult>();
  connect(&correlator_, &CommandCorrelator::CommandCompleted, this,
          &TalkCoreClient::CommandCompleted);
  connect(&correlator_, &CommandCorrelator::CommandTimedOut, this,
          &TalkCoreClient::CommandTimedOut);
}

TalkCoreClient::~TalkCoreClient() {
  Stop();
}

bool TalkCoreClient::Start(const QJsonObject& config) {
  Stop();
  ++generation_;
  context_ = std::make_unique<Context>(Context{this, generation_});
  const QByteArray text = QJsonDocument(config).toJson(QJsonDocument::Compact);
  talk_options options{};
  options.struct_size = sizeof(talk_options);
  options.abi_version = TALK_ABI_VERSION;
  options.on_event = &TalkCoreClient::Dispatch;
  options.userdata = context_.get();
  options.config_json = text.constData();
  core_ = talk_create(&options);
  if (core_ == nullptr) {
    context_.reset();
    return false;
  }
  return true;
}

void TalkCoreClient::Stop() {
  ++generation_;
  if (core_ != nullptr) {
    talk_set_video_frame_callback(core_, nullptr, nullptr);
    talk_set_local_video_frame_callback(core_, nullptr, nullptr);
    talk_destroy(core_);
    core_ = nullptr;
  }
  correlator_.CancelAll();
  context_.reset();
}

quint64 TalkCoreClient::Submit(const QString& method, const QJsonObject& data,
                               int timeout_ms) {
  if (core_ == nullptr || method.isEmpty()) return 0;
  const QByteArray json = QJsonDocument(data).toJson(QJsonDocument::Compact);
  const QByteArray method_text = method.toUtf8();
  const quint64 id = talk_command(core_, method_text.constData(), json.constData());
  if (id != 0) correlator_.Track(id, method, timeout_ms);
  return id;
}

void TalkCoreClient::SetVideoCallback(bool enabled) {
  if (core_ == nullptr) return;
  talk_set_video_frame_callback(core_,
                                enabled ? &TalkCoreClient::DispatchVideo : nullptr,
                                enabled ? context_.get() : nullptr);
}

void TalkCoreClient::SetLocalVideoCallback(bool enabled) {
  if (core_ == nullptr) return;
  talk_set_local_video_frame_callback(
      core_, enabled ? &TalkCoreClient::DispatchLocalVideo : nullptr,
      enabled ? context_.get() : nullptr);
}

void TalkCoreClient::Dispatch(void* context, const char* name, const char* json) {
  if (context == nullptr || name == nullptr || json == nullptr) return;
  const Context state = *static_cast<Context*>(context);
  const QString event_name = QString::fromUtf8(name);
  const QJsonObject data = QJsonDocument::fromJson(QByteArray(json)).object();
  QMetaObject::invokeMethod(
      state.self,
      [state, event_name, data] {
        if (state.self->generation_ != state.generation) return;
        state.self->DeliverEvent(event_name, data);
      },
      Qt::QueuedConnection);
}

void TalkCoreClient::DispatchVideo(void* context, const char* mid,
                                   const uint8_t* bgra, int width, int height,
                                   int stride, int64_t timestamp_us) {
  Q_UNUSED(timestamp_us);
  if (context == nullptr || bgra == nullptr || width <= 0 || height <= 0 ||
      stride < width * 4) {
    return;
  }
  const Context state = *static_cast<Context*>(context);
  const QImage view(bgra, width, height, stride, QImage::Format_ARGB32);
  const QImage frame = view.copy();
  const QString mid_text = QString::fromUtf8(mid != nullptr ? mid : "");
  QMetaObject::invokeMethod(
      state.self,
      [state, frame, mid_text] {
        if (state.self->generation_ != state.generation) return;
        state.self->DeliverVideo(frame, mid_text);
      },
      Qt::QueuedConnection);
}

void TalkCoreClient::DispatchLocalVideo(void* context, const char* mid,
                                        const uint8_t* bgra, int width,
                                        int height, int stride,
                                        int64_t timestamp_us) {
  Q_UNUSED(timestamp_us);
  if (context == nullptr || bgra == nullptr || width <= 0 || height <= 0 ||
      stride < width * 4) {
    return;
  }
  const Context state = *static_cast<Context*>(context);
  const QImage view(bgra, width, height, stride, QImage::Format_ARGB32);
  const QImage frame = view.copy();
  const QString mid_text = QString::fromUtf8(mid != nullptr ? mid : "");
  QMetaObject::invokeMethod(
      state.self,
      [state, frame, mid_text] {
        if (state.self->generation_ != state.generation) return;
        state.self->DeliverLocalVideo(frame, mid_text);
      },
      Qt::QueuedConnection);
}

void TalkCoreClient::DeliverEvent(const QString& name, const QJsonObject& data) {
  const CoreEvent event = EventMapper::Map(name, data);
  if (const auto* result = std::get_if<ResultEvent>(&event.payload)) {
    correlator_.Resolve(*result);
  }
  emit CoreEventReceived(event);
}

void TalkCoreClient::DeliverVideo(const QImage& frame, const QString& mid) {
  emit VideoFrameReceived(frame, mid);
}

void TalkCoreClient::DeliverLocalVideo(const QImage& frame,
                                       const QString& mid) {
  emit LocalVideoFrameReceived(frame, mid);
}

}  // namespace talk::desktop
