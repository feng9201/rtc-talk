#include "rtcwin/webrtc_media/audio_media_session.h"

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>

std::string EnvironmentValue(const char* name) {
  char* value = nullptr;
  size_t length = 0;
  if (_dupenv_s(&value, &length, name) != 0 || value == nullptr) return {};
  std::string result(value);
  free(value);
  return result;
}

int main() {
  rtcwin::webrtc_media::AudioMediaSession session;
  rtcwin::webrtc_media::AudioMediaConfig config;
  const std::string backend = EnvironmentValue("RTCWIN_AUDIO_BACKEND");
  const std::string audio_source = EnvironmentValue("RTCWIN_AUDIO_SOURCE");
  config.audio_backend = backend.empty() ? "fake" : backend;
  config.audio_source = audio_source.empty() ? "mic" : audio_source;
  config.volume = 55;
  config.muted = true;

  rtcwin::webrtc_media::AudioMediaError error;
  if (!session.Initialize(config, [](std::string, std::string) {}, &error)) {
    std::cerr << "initialize failed: " << error.code << " " << error.message
              << '\n';
    return 1;
  }

  rtcwin::webrtc_media::AudioSfuOffer sfu_offer;
  if (!session.CreateSfuPublishOffer(&sfu_offer, &error) ||
      sfu_offer.sdp.find("m=audio") == std::string::npos) {
    std::cerr << "SFU offer failed: " << error.code << " " << error.message
              << '\n';
    return 2;
  }
  rtcwin::webrtc_media::AudioMediaSession peer;
  rtcwin::webrtc_media::AudioMediaConfig peer_config = config;
  peer_config.audio_backend = "fake";
  peer_config.audio_source = "mic";
  if (!peer.Initialize(peer_config, [](std::string, std::string) {}, &error)) {
    std::cerr << "peer initialize failed: " << error.code << " "
              << error.message << '\n';
    return 3;
  }
  std::string sfu_answer;
  if (!peer.CreateSfuSubscribeAnswer(sfu_offer.sdp, &sfu_answer, &error) ||
      sfu_answer.find("m=audio") == std::string::npos ||
      !session.ApplySfuPublishAnswer(sfu_answer, &error)) {
    std::cerr << "SFU negotiation failed: " << error.code << " "
              << error.message << '\n';
    return 4;
  }
  if (config.audio_backend != "fake") {
    if (!session.SetMuted(false, &error)) {
      std::cerr << "real audio unmute failed: " << error.code << " "
                << error.message << '\n';
      return 5;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    const auto status = session.status();
    std::cout << "real audio device smoke ok"
              << " sfu_sdp=" << sfu_offer.sdp.size()
              << " recording=" << status.recording_device
              << " playout=" << status.playout_device
              << " recording_initialized=" << status.recording_initialized
              << " recording=" << status.recording
              << " capture_frames=" << status.capture_frames
              << " sfu_send_active=" << status.sfu_send_active
              << " sfu_packets_sent=" << status.sfu_audio_packets_sent
              << '\n';
    peer.Shutdown();
    session.Shutdown();
    return 0;
  }

  std::string p2p_offer;
  if (!session.CreateP2pOffer(&p2p_offer, &error) ||
      p2p_offer.find("m=audio") == std::string::npos) {
    std::cerr << "P2P offer failed: " << error.code << " " << error.message
              << '\n';
    return 5;
  }
  std::string p2p_answer;
  if (!peer.CreateP2pAnswer(p2p_offer, &p2p_answer, &error) ||
      p2p_answer.find("m=audio") == std::string::npos ||
      !session.ApplyP2pAnswer(p2p_answer, &error)) {
    std::cerr << "P2P negotiation failed: " << error.code << " "
              << error.message << '\n';
    return 6;
  }

  if (!session.SetMuted(false, &error) ||
      !session.SetDeafened(true, &error) ||
      !session.SetSfuPaused(true, &error) ||
      !session.SetVolume(0.7, &error)) {
    std::cerr << "audio control failed: " << error.code << " " << error.message
              << '\n';
    return 7;
  }

  const auto status = session.status();
  if (!status.initialized || !status.sfu_send_ready || !status.p2p_ready ||
      status.muted || !status.deafened || !status.sfu_paused) {
    std::cerr << "unexpected media status\n";
    return 8;
  }

  std::cout << "audio media smoke ok"
            << " sfu_sdp=" << sfu_offer.sdp.size()
            << " p2p_sdp=" << p2p_offer.size()
            << " recording=" << status.recording_device
            << " playout=" << status.playout_device << '\n';
  peer.Shutdown();
  session.Shutdown();
  return 0;
}
