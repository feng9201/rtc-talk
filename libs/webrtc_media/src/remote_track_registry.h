#pragma once

#include "rtcwin/webrtc_media/media_session.h"

#include "api/media_stream_interface.h"
#include "rtc_base/thread.h"

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace rtcwin::webrtc_media {

class RemoteVideoSink;

class RemoteTrackRegistry final {
 public:
  struct Counts {
    std::uint64_t tracks = 0;
    std::uint64_t active_tracks = 0;
    std::uint64_t video_tracks = 0;
    std::uint64_t active_video_tracks = 0;
  };

  explicit RemoteTrackRegistry(webrtc::Thread* signaling);
  ~RemoteTrackRegistry();

  RemoteTrackRegistry(const RemoteTrackRegistry&) = delete;
  RemoteTrackRegistry& operator=(const RemoteTrackRegistry&) = delete;

  void SetVideoCallback(std::function<void(RemoteVideoFrame)> callback);
  void SetTrackInfo(const std::vector<RemoteTrackInfo>& consumers);

  void AddAudioTrack(
      const std::string& pc, const std::string& mid,
      webrtc::scoped_refptr<webrtc::AudioTrackInterface> track);
  void AddVideoTrack(
      const std::string& pc, const std::string& mid,
      webrtc::scoped_refptr<webrtc::VideoTrackInterface> track);

  void SetVolume(double volume);
  void SetDeafened(bool deafened);
  void SetSfuPaused(bool paused);
  void Remove(const std::string& pc);
  void Clear();
  Counts counts() const;

 private:
  struct AudioEntry {
    std::string pc;
    std::string mid;
    webrtc::scoped_refptr<webrtc::AudioTrackInterface> track;
  };
  struct VideoEntry {
    std::string pc;
    std::string mid;
    webrtc::scoped_refptr<webrtc::VideoTrackInterface> track;
    std::unique_ptr<RemoteVideoSink> sink;
  };

  RemoteTrackInfo TrackInfoFor(const std::string& pc,
                               const std::string& mid) const;
  void ApplyAudioState(
      const webrtc::scoped_refptr<webrtc::AudioTrackInterface>& track,
      const std::string& pc) const;
  void ApplyVideoState(
      const webrtc::scoped_refptr<webrtc::VideoTrackInterface>& track) const;
  void RemoveVideoEntriesOnSignaling(std::vector<VideoEntry> entries);
  template <class F>
  void RunOnSignaling(F&& task) const {
    if (signaling_ && !signaling_->IsCurrent()) {
      signaling_->BlockingCall(std::forward<F>(task));
    } else {
      task();
    }
  }

  webrtc::Thread* signaling_ = nullptr;
  mutable std::mutex mutex_;
  std::function<void(RemoteVideoFrame)> video_callback_;
  std::vector<AudioEntry> audio_tracks_;
  std::vector<VideoEntry> video_tracks_;
  std::map<std::string, RemoteTrackInfo> track_info_;
  double volume_ = 0.6;
  bool deafened_ = false;
  bool sfu_paused_ = false;
};

}  // namespace rtcwin::webrtc_media
