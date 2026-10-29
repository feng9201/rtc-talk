#include "sdp_negotiator.h"

#include "media_error.h"

#include "api/jsep.h"
#include "api/make_ref_counted.h"

#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <utility>

namespace rtcwin::webrtc_media {
namespace {

constexpr auto kSdpTimeout = std::chrono::seconds(5);

struct CreateDescriptionState {
  std::mutex mutex;
  std::condition_variable cv;
  bool done = false;
  std::unique_ptr<webrtc::SessionDescriptionInterface> description;
};

class CreateDescriptionObserver
    : public webrtc::CreateSessionDescriptionObserver {
 public:
  explicit CreateDescriptionObserver(
      std::shared_ptr<CreateDescriptionState> state)
      : state_(std::move(state)) {}

  void OnSuccess(webrtc::SessionDescriptionInterface* description) override {
    std::lock_guard<std::mutex> lock(state_->mutex);
    state_->description.reset(description);
    state_->done = true;
    state_->cv.notify_all();
  }

  void OnFailure(webrtc::RTCError) override {
    std::lock_guard<std::mutex> lock(state_->mutex);
    state_->description.reset();
    state_->done = true;
    state_->cv.notify_all();
  }

 private:
  std::shared_ptr<CreateDescriptionState> state_;
};

struct SetDescriptionState {
  std::mutex mutex;
  std::condition_variable cv;
  bool done = false;
  bool ok = false;
};

class SetDescriptionObserver : public webrtc::SetSessionDescriptionObserver {
 public:
  explicit SetDescriptionObserver(std::shared_ptr<SetDescriptionState> state)
      : state_(std::move(state)) {}

  void OnSuccess() override {
    std::lock_guard<std::mutex> lock(state_->mutex);
    state_->ok = true;
    state_->done = true;
    state_->cv.notify_all();
  }

  void OnFailure(webrtc::RTCError) override {
    std::lock_guard<std::mutex> lock(state_->mutex);
    state_->ok = false;
    state_->done = true;
    state_->cv.notify_all();
  }

 private:
  std::shared_ptr<SetDescriptionState> state_;
};

std::unique_ptr<webrtc::SessionDescriptionInterface> WaitForDescription(
    const std::shared_ptr<CreateDescriptionState>& state) {
  std::unique_lock<std::mutex> lock(state->mutex);
  if (!state->cv.wait_for(lock, kSdpTimeout, [&] { return state->done; })) {
    return nullptr;
  }
  return std::move(state->description);
}

bool WaitForSetDescription(const std::shared_ptr<SetDescriptionState>& state) {
  std::unique_lock<std::mutex> lock(state->mutex);
  return state->cv.wait_for(lock, kSdpTimeout,
                            [&] { return state->done; }) &&
         state->ok;
}

}  // namespace

bool CreateAndSetLocalDescription(
    webrtc::Thread* signaling,
    webrtc::scoped_refptr<webrtc::PeerConnectionInterface> pc, bool offer,
    MediaError* error) {
  if (!signaling || !pc) {
    SetError(error, 1705, "PeerConnection 尚未创建");
    return false;
  }
  auto create_state = std::make_shared<CreateDescriptionState>();
  auto observer =
      webrtc::make_ref_counted<CreateDescriptionObserver>(create_state);
  const bool requested = signaling->BlockingCall([pc, offer, &observer] {
    webrtc::PeerConnectionInterface::RTCOfferAnswerOptions options;
    if (offer) {
      pc->CreateOffer(observer.get(), options);
    } else {
      pc->CreateAnswer(observer.get(), options);
    }
    return true;
  });
  if (!requested) {
    SetError(error, 1705, "PeerConnection 尚未创建");
    return false;
  }
  auto description = WaitForDescription(create_state);
  if (!description) {
    SetError(error, 1706,
             offer ? "生成 offer 超时" : "生成 answer 超时");
    return false;
  }

  auto set_state = std::make_shared<SetDescriptionState>();
  auto set_observer =
      webrtc::make_ref_counted<SetDescriptionObserver>(set_state);
  auto* raw = description.release();
  const bool posted = signaling->BlockingCall([pc, &set_observer, raw] {
    if (!pc) {
      delete raw;
      return false;
    }
    pc->SetLocalDescription(set_observer.get(), raw);
    return true;
  });
  if (!posted) {
    SetError(error, 1707, "设置本地 SDP 失败");
    return false;
  }
  if (!WaitForSetDescription(set_state)) {
    SetError(error, 1708, "应用本地 SDP 失败");
    return false;
  }
  return true;
}

bool SetRemoteDescription(
    webrtc::Thread* signaling,
    webrtc::scoped_refptr<webrtc::PeerConnectionInterface> pc,
    webrtc::SdpType type, const std::string& sdp, MediaError* error) {
  if (!signaling || !pc) {
    SetError(error, 1711, "PeerConnection 尚未创建");
    return false;
  }
  if (sdp.empty()) {
    SetError(error, 1709, "远端 SDP 为空");
    return false;
  }
  auto description = webrtc::CreateSessionDescription(type, sdp);
  if (!description) {
    SetError(error, 1710, "解析远端 SDP 失败");
    return false;
  }
  auto set_state = std::make_shared<SetDescriptionState>();
  auto observer =
      webrtc::make_ref_counted<SetDescriptionObserver>(set_state);
  auto* raw = description.release();
  const bool posted = signaling->BlockingCall([pc, &observer, raw] {
    if (!pc) {
      delete raw;
      return false;
    }
    pc->SetRemoteDescription(observer.get(), raw);
    return true;
  });
  if (!posted) {
    SetError(error, 1711, "PeerConnection 尚未创建");
    return false;
  }
  if (!WaitForSetDescription(set_state)) {
    SetError(error, 1712, "应用远端 SDP 失败");
    return false;
  }
  return true;
}

}  // namespace rtcwin::webrtc_media
