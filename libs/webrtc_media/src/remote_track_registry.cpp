#include "remote_track_registry.h"

#include "remote_video_sink.h"

#include <algorithm>
#include <utility>

namespace rtcwin::webrtc_media {

RemoteTrackRegistry::RemoteTrackRegistry(webrtc::Thread* signaling)
    : signaling_(signaling) {}

RemoteTrackRegistry::~RemoteTrackRegistry() { Clear(); }

void RemoteTrackRegistry::SetVideoCallback(
    std::function<void(RemoteVideoFrame)> callback) {
  std::lock_guard<std::mutex> lock(mutex_);
  video_callback_ = std::move(callback);
}

void RemoteTrackRegistry::SetTrackInfo(
    const std::vector<RemoteTrackInfo>& consumers) {
  std::lock_guard<std::mutex> lock(mutex_);
  for (const auto& consumer : consumers) {
    if (consumer.mid.empty()) continue;
    track_info_[consumer.mid] = consumer;
  }
}

void RemoteTrackRegistry::SetP2pPeerUid(std::string uid) {
  std::lock_guard<std::mutex> lock(mutex_);
  p2p_peer_uid_ = std::move(uid);
}

void RemoteTrackRegistry::AddAudioTrack(
    const std::string& pc, const std::string& mid,
    webrtc::scoped_refptr<webrtc::AudioTrackInterface> track) {
  if (!track) return;
  RunOnSignaling([this, &pc, track] { ApplyAudioState(track, pc); });
  std::lock_guard<std::mutex> lock(mutex_);
  for (auto& entry : audio_tracks_) {
    if (entry.pc == pc && entry.mid == mid) {
      entry.track = std::move(track);
      return;
    }
  }
  audio_tracks_.push_back(AudioEntry{pc, mid, std::move(track)});
}

void RemoteTrackRegistry::AddVideoTrack(
    const std::string& pc, const std::string& mid,
    webrtc::scoped_refptr<webrtc::VideoTrackInterface> track) {
  if (!track) return;
  auto sink = std::make_unique<RemoteVideoSink>(
      mid,
      [this, pc, mid] { return TrackInfoFor(pc, mid); },
      [this](RemoteVideoFrame frame) {
        std::function<void(RemoteVideoFrame)> callback;
        {
          std::lock_guard<std::mutex> lock(mutex_);
          callback = video_callback_;
        }
        if (callback) callback(std::move(frame));
      });

  std::lock_guard<std::mutex> lock(mutex_);
  for (auto& entry : video_tracks_) {
    if (entry.pc == pc && entry.mid == mid) {
      if (entry.track && entry.sink) {
        entry.track->RemoveSink(entry.sink.get());
      }
      entry.track = track;
      entry.sink = std::move(sink);
      track->AddOrUpdateSink(entry.sink.get(), webrtc::VideoSinkWants());
      ApplyVideoState(track, pc);
      return;
    }
  }
  track->AddOrUpdateSink(sink.get(), webrtc::VideoSinkWants());
  ApplyVideoState(track, pc);
  video_tracks_.push_back(
      VideoEntry{pc, mid, std::move(track), std::move(sink)});
}

void RemoteTrackRegistry::SetVolume(double volume) {
  std::vector<AudioEntry> tracks;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    volume_ = volume;
    tracks = audio_tracks_;
  }
  RunOnSignaling([this, tracks = std::move(tracks)] {
    for (const auto& entry : tracks) {
      ApplyAudioState(entry.track, entry.pc);
    }
  });
}

void RemoteTrackRegistry::SetDeafened(bool deafened) {
  std::vector<AudioEntry> tracks;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    deafened_ = deafened;
    tracks = audio_tracks_;
  }
  RunOnSignaling([this, tracks = std::move(tracks)] {
    for (const auto& entry : tracks) {
      ApplyAudioState(entry.track, entry.pc);
    }
  });
}

void RemoteTrackRegistry::SetSfuPaused(bool paused) {
  std::vector<AudioEntry> audio_tracks;
  struct VideoStateEntry {
    std::string pc;
    webrtc::scoped_refptr<webrtc::VideoTrackInterface> track;
  };
  std::vector<VideoStateEntry> video_tracks;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    sfu_paused_ = paused;
    audio_tracks = audio_tracks_;
    for (const auto& entry : video_tracks_) {
      if (entry.track) {
        video_tracks.push_back(VideoStateEntry{entry.pc, entry.track});
      }
    }
  }
  RunOnSignaling([this, audio_tracks = std::move(audio_tracks),
                  video_tracks = std::move(video_tracks)] {
    for (const auto& entry : audio_tracks) {
      ApplyAudioState(entry.track, entry.pc);
    }
    for (const auto& entry : video_tracks) {
      ApplyVideoState(entry.track, entry.pc);
    }
  });
}

void RemoteTrackRegistry::Remove(const std::string& pc) {
  std::vector<VideoEntry> removed_video;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    audio_tracks_.erase(
        std::remove_if(audio_tracks_.begin(), audio_tracks_.end(),
                       [&pc](const AudioEntry& entry) {
                         return entry.pc == pc;
                       }),
        audio_tracks_.end());
    for (auto it = video_tracks_.begin(); it != video_tracks_.end();) {
      if (it->pc == pc) {
        removed_video.push_back(std::move(*it));
        it = video_tracks_.erase(it);
      } else {
        ++it;
      }
    }
  }
  RemoveVideoEntriesOnSignaling(std::move(removed_video));
}

void RemoteTrackRegistry::Clear() {
  std::vector<VideoEntry> removed_video;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    audio_tracks_.clear();
    removed_video.swap(video_tracks_);
    track_info_.clear();
  }
  RemoveVideoEntriesOnSignaling(std::move(removed_video));
}

RemoteTrackRegistry::Counts RemoteTrackRegistry::counts() const {
  Counts result;
  std::lock_guard<std::mutex> lock(mutex_);
  result.tracks = audio_tracks_.size();
  for (const auto& entry : audio_tracks_) {
    if (entry.track && entry.track->enabled()) ++result.active_tracks;
  }
  result.video_tracks = video_tracks_.size();
  for (const auto& entry : video_tracks_) {
    if (entry.track && entry.track->enabled()) {
      ++result.active_video_tracks;
    }
  }
  return result;
}

RemoteTrackInfo RemoteTrackRegistry::TrackInfoFor(
    const std::string& pc, const std::string& mid) const {
  std::lock_guard<std::mutex> lock(mutex_);
  const auto it = track_info_.find(mid);
  if (it != track_info_.end()) return it->second;
  RemoteTrackInfo info;
  info.mid = mid;
  info.kind = "video";
  info.source = "camera";
  if (pc == "p2p") {
    info.uid = p2p_peer_uid_.empty() ? "p2p" : p2p_peer_uid_;
    if (mid == "2") info.source = "screen";
  }
  return info;
}

void RemoteTrackRegistry::ApplyAudioState(
    const webrtc::scoped_refptr<webrtc::AudioTrackInterface>& track,
    const std::string& pc) const {
  if (!track) return;
  if (auto source = track->GetSource()) {
    source->SetVolume(volume_);
  }
  const bool paused = deafened_ || (pc == "sfu-recv" && sfu_paused_);
  track->set_enabled(!paused);
}

void RemoteTrackRegistry::ApplyVideoState(
    const webrtc::scoped_refptr<webrtc::VideoTrackInterface>& track,
    const std::string& pc) const {
  if (!track) return;
  track->set_enabled(pc != "sfu-recv" || !sfu_paused_);
}

void RemoteTrackRegistry::RemoveVideoEntriesOnSignaling(
    std::vector<VideoEntry> entries) {
  if (!signaling_) return;
  signaling_->BlockingCall([entries = std::move(entries)]() mutable {
    for (auto& entry : entries) {
      if (entry.track && entry.sink) {
        entry.track->RemoveSink(entry.sink.get());
      }
    }
  });
}

}  // namespace rtcwin::webrtc_media
