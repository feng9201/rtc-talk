#include "state/video_stream_registry.h"

#include <QDateTime>

#include <algorithm>

namespace talk::desktop {
namespace {

constexpr int kFrameFlushMs = 33;

QString SourceKey(const QString& uid, VideoSource source) {
  return uid + QLatin1Char('|') +
         (source == VideoSource::Screen ? QStringLiteral("screen")
                                        : QStringLiteral("camera"));
}

}  // namespace

VideoStreamRegistry::VideoStreamRegistry(QObject* parent,
                                         int no_frame_timeout_ms)
    : QObject(parent),
      no_frame_timeout_ms_(std::max(1, no_frame_timeout_ms)) {
  qRegisterMetaType<VideoStreamView>();
  qRegisterMetaType<QVector<VideoStreamView>>();

  frame_timer_.setSingleShot(true);
  frame_timer_.setInterval(kFrameFlushMs);
  connect(&frame_timer_, &QTimer::timeout, this,
          &VideoStreamRegistry::FlushFrames);

  stale_timer_.setInterval(std::max(20, std::min(500, no_frame_timeout_ms_ / 2)));
  connect(&stale_timer_, &QTimer::timeout, this,
          &VideoStreamRegistry::SweepStaleFrames);
  stale_timer_.start();
}

void VideoStreamRegistry::ApplyEvent(const CoreEvent& event) {
  if (const auto* track = std::get_if<VideoTrackEvent>(&event.payload)) {
    if (track->mid.isEmpty()) return;
    VideoStreamView& stream = Upsert(track->mid);
    const bool changed =
        stream.uid != track->uid || stream.source != track->source ||
        stream.paused;
    stream.uid = track->uid;
    stream.source = track->source;
    stream.paused =
        paused_by_mid_.value(track->mid,
                             paused_by_source_.value(
                                 SourceKey(track->uid, track->source), false));
    if (changed) NotifyStreams();
    return;
  }

  if (const auto* producer =
          std::get_if<ProducerStateEvent>(&event.payload)) {
    if (producer->kind != QStringLiteral("video")) return;
    paused_by_source_.insert(SourceKey(producer->uid, producer->source),
                             producer->paused);
    if (!producer->mid.isEmpty()) {
      paused_by_mid_.insert(producer->mid, producer->paused);
    }
    VideoStreamView* stream = nullptr;
    if (!producer->mid.isEmpty()) {
      stream = Find(producer->mid);
    }
    if (stream == nullptr) {
      for (VideoStreamView& candidate : streams_) {
        if (candidate.uid == producer->uid &&
            candidate.source == producer->source) {
          stream = &candidate;
          break;
        }
      }
    }
    if (stream == nullptr) return;
    if (stream->paused == producer->paused) return;
    stream->paused = producer->paused;
    if (stream->paused) {
      stream->receiving = false;
      stream->lastFrameMs = 0;
      stream->frame = QImage();
      pending_frames_.remove(stream->mid);
      emit FrameCleared(stream->mid);
    }
    NotifyStreams();
    return;
  }

  if (std::get_if<JoinedEvent>(&event.payload) != nullptr ||
      std::get_if<LeftEvent>(&event.payload) != nullptr ||
      std::get_if<KickedEvent>(&event.payload) != nullptr) {
    Clear();
    return;
  }
  if (const auto* state = std::get_if<StateEvent>(&event.payload)) {
    if (state->state == ConnectionState::Disconnected) Clear();
  }
}

void VideoStreamRegistry::FrameReceived(const QImage& frame,
                                        const QString& mid) {
  if (frame.isNull() || mid.isEmpty()) return;
  VideoStreamView& stream = Upsert(mid);
  // producer 暂停后仍可能有在途帧，必须丢弃，不能用最后一帧重新点亮画面。
  if (stream.paused) return;

  const bool newly_receiving = !stream.receiving;
  stream.receiving = true;
  stream.lastFrameMs = QDateTime::currentMSecsSinceEpoch();
  stream.frame = frame;
  pending_frames_.insert(mid, frame);
  if (newly_receiving) NotifyStreams();
  if (!frame_timer_.isActive()) frame_timer_.start();
}

void VideoStreamRegistry::Clear() {
  const bool had_state =
      !streams_.isEmpty() || !pending_frames_.isEmpty() ||
      !paused_by_mid_.isEmpty() || !paused_by_source_.isEmpty();
  streams_.clear();
  pending_frames_.clear();
  paused_by_mid_.clear();
  paused_by_source_.clear();
  frame_timer_.stop();
  if (had_state) NotifyStreams();
}

VideoStreamView* VideoStreamRegistry::Find(const QString& mid) {
  for (VideoStreamView& stream : streams_) {
    if (stream.mid == mid) return &stream;
  }
  return nullptr;
}

VideoStreamView& VideoStreamRegistry::Upsert(const QString& mid) {
  if (VideoStreamView* stream = Find(mid)) return *stream;
  VideoStreamView stream;
  stream.mid = mid;
  streams_.append(std::move(stream));
  return streams_.last();
}

void VideoStreamRegistry::FlushFrames() {
  if (pending_frames_.isEmpty()) return;
  const auto pending = pending_frames_;
  pending_frames_.clear();
  for (auto it = pending.begin(); it != pending.end(); ++it) {
    emit FrameReady(it.key(), it.value());
  }
}

void VideoStreamRegistry::SweepStaleFrames() {
  const qint64 now = QDateTime::currentMSecsSinceEpoch();
  bool changed = false;
  for (VideoStreamView& stream : streams_) {
    if (!stream.receiving ||
        now - stream.lastFrameMs < no_frame_timeout_ms_) {
      continue;
    }
    stream.receiving = false;
    stream.frame = QImage();
    pending_frames_.remove(stream.mid);
    emit FrameCleared(stream.mid);
    changed = true;
  }
  if (changed) NotifyStreams();
}

void VideoStreamRegistry::NotifyStreams() {
  emit StreamsChanged(streams_);
}

}  // namespace talk::desktop
