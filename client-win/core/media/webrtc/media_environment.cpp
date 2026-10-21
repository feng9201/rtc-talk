// File: core/media/webrtc/media_environment.cpp
// Brief: 初始化 libwebrtc 线程、工厂、APM 与音频设备。
#include "media/webrtc/media_environment.h"

#include <memory>
#include <string>
#include <utility>

#include "api/audio/audio_processing.h"
#include "api/audio/builtin_audio_processing_builder.h"
#include "api/audio_codecs/audio_decoder_factory_template.h"
#include "api/audio_codecs/audio_encoder_factory_template.h"
#include "api/audio_codecs/opus/audio_decoder_opus.h"
#include "api/audio_codecs/opus/audio_encoder_opus.h"
#include "api/create_modular_peer_connection_factory.h"
#include "api/enable_media.h"
#include "api/environment/environment_factory.h"
#include "media/webrtc/audio_device.h"
#include "media/webrtc/capture_gate.h"
#include "rtc_base/logging.h"

namespace talk {
namespace {

void SetError(std::string* error, const std::string& message) {
  if (error != nullptr) {
    *error = message;
  }
}

}  // namespace

MediaEnvironment::~MediaEnvironment() {
  // CaptureGate 持有采集计数指针，必须先于 AudioDevice 销毁。
  factory_ = nullptr;
  audio_.reset();
  signaling_.reset();
  worker_.reset();
  network_.reset();
}

std::unique_ptr<MediaEnvironment> MediaEnvironment::Create(
    const Json& config, const Json& ice_servers, MuteGate gate,
    std::string* error) {
  auto environment = std::unique_ptr<MediaEnvironment>(new MediaEnvironment());
  if (!environment->Initialize(config, ice_servers, std::move(gate), error)) {
    return nullptr;
  }
  return environment;
}

bool MediaEnvironment::Initialize(const Json& config, const Json& ice_servers,
                                  MuteGate gate, std::string* error) {
  std::string backend = "none";
  if (config.contains("audio_backend")) {
    if (!config["audio_backend"].is_string()) {
      SetError(error, "audio_backend 必须是字符串");
      return false;
    }
    backend = config["audio_backend"].get<std::string>();
  }
  if (backend != "fake" && backend != "wasapi" && backend != "default") {
    SetError(error, "未启用音频后端");
    return false;
  }
  std::string audio_source = "mic";
  if (config.contains("audio_source")) {
    if (!config["audio_source"].is_string()) {
      SetError(error, "audio_source 必须是字符串");
      return false;
    }
    audio_source = config["audio_source"].get<std::string>();
    if (audio_source != "mic" && audio_source != "system") {
      SetError(error, "audio_source 必须是 mic/system");
      return false;
    }
  }

  // 默认不输出 WebRTC 的 SDP/候选日志，避免凭证进入诊断输出。
  const bool verbose = config.value("webrtc_log", false);
  webrtc::LogMessage::LogToDebug(verbose ? webrtc::LS_INFO : webrtc::LS_NONE);
  if (verbose) {
    webrtc::LogMessage::LogTimestamps();
    webrtc::LogMessage::LogThreads();
  }

  network_ = webrtc::Thread::CreateWithSocketServer();
  worker_ = webrtc::Thread::Create();
  signaling_ = webrtc::Thread::Create();
  if (!network_ || !worker_ || !signaling_) {
    SetError(error, "线程创建失败");
    return false;
  }
  network_->SetName("talk-network", nullptr);
  worker_->SetName("talk-worker", nullptr);
  signaling_->SetName("talk-signaling", nullptr);
  if (!network_->Start() || !worker_->Start() || !signaling_->Start()) {
    SetError(error, "线程启动失败");
    return false;
  }

  const webrtc::Environment env = webrtc::CreateEnvironment();
  audio_ =
      AudioDevice::Create(backend, audio_source, env, worker_.get(), error);
  if (!audio_) {
    return false;
  }

  webrtc::PeerConnectionFactoryDependencies deps;
  deps.network_thread = network_.get();
  deps.worker_thread = worker_.get();
  deps.signaling_thread = signaling_.get();
  deps.env = env;
  deps.adm = audio_->adm();

  webrtc::AudioProcessing::Config apm;
  const bool real_device = backend != "fake";
  // 系统音不经过麦克风，关闭回声/降噪/AGC，避免对 loopback 数据做无参考处理。
  const bool use_apm = real_device && audio_source != "system";
  apm.echo_canceller.enabled = use_apm;
  apm.noise_suppression.enabled = use_apm;
  apm.gain_controller2.enabled = use_apm;
  apm.high_pass_filter.enabled = true;
  if (use_apm) {
    deps.audio_processing_builder =
        std::make_unique<webrtc::BuiltinAudioProcessingBuilder>(apm);
  }
  deps.audio_encoder_factory =
      webrtc::CreateAudioEncoderFactory<webrtc::AudioEncoderOpus>();
  deps.audio_decoder_factory =
      webrtc::CreateAudioDecoderFactory<webrtc::AudioDecoderOpus>();
  deps.audio_frame_processor = std::make_unique<CaptureGate>(
      std::move(gate), audio_->capture_frames());

  webrtc::EnableMedia(deps);
  factory_ = webrtc::CreateModularPeerConnectionFactory(std::move(deps));
  if (!factory_) {
    SetError(error, "PeerConnectionFactory 创建失败");
    return false;
  }

  rtc_config_.sdp_semantics = webrtc::SdpSemantics::kUnifiedPlan;
  ConfigureIceServers(ice_servers);
  return true;
}

bool MediaEnvironment::ConfigureIceServers(const Json& ice_servers) {
  if (!ice_servers.is_array()) {
    return true;
  }
  for (const auto& item : ice_servers) {
    if (!item.is_object() || !item.contains("urls")) {
      continue;
    }
    webrtc::PeerConnectionInterface::IceServer server;
    const auto& urls = item["urls"];
    if (urls.is_array()) {
      for (const auto& url : urls) {
        if (url.is_string()) {
          server.urls.push_back(url.get<std::string>());
        }
      }
    } else if (urls.is_string()) {
      server.urls.push_back(urls.get<std::string>());
    }
    if (server.urls.empty()) {
      continue;
    }
    if (item.contains("username") && item["username"].is_string()) {
      server.username = item["username"].get<std::string>();
    }
    if (item.contains("credential") && item["credential"].is_string()) {
      server.password = item["credential"].get<std::string>();
    }
    rtc_config_.servers.push_back(std::move(server));
  }
  return true;
}

}  // namespace talk
