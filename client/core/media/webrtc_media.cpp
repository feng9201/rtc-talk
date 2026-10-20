#include "media.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <future>
#include <map>
#include <thread>
#include "api/audio/audio_device.h"
#include "api/audio/audio_frame.h"
#include "api/audio/audio_frame_processor.h"
#include <mutex>
#include "api/audio/audio_processing.h"
#include "api/audio/builtin_audio_processing_builder.h"
#include "api/audio/create_audio_device_module.h"
#include "api/audio_codecs/audio_decoder_factory_template.h"
#include "api/audio_codecs/audio_encoder_factory_template.h"
#include "api/audio_codecs/opus/audio_decoder_opus.h"
#include "api/audio_codecs/opus/audio_encoder_opus.h"
#include "api/audio_options.h"
#include "api/create_modular_peer_connection_factory.h"
#include "api/enable_media.h"
#include "api/environment/environment_factory.h"
#include "api/jsep.h"
#include "api/make_ref_counted.h"
#include "api/peer_connection_interface.h"
#include "api/rtp_transceiver_interface.h"
#include "api/stats/rtc_stats_collector_callback.h"
#include "api/stats/rtc_stats_report.h"
#include "api/stats/rtcstats_objects.h"
#include "modules/audio_device/include/audio_device_default.h"
#include "rtc_base/logging.h"
#include "rtc_base/thread.h"
namespace talk {
namespace {
using namespace std::chrono_literals;
// 编码前执行安全静音，不等待 SDP/设备操作队列。
class CaptureGate : public webrtc::AudioFrameProcessor {
 public:
  explicit CaptureGate(MuteGate gate):gate_(std::move(gate)){}
  void Process(std::unique_ptr<webrtc::AudioFrame> frame) override {
    if(gate_->load())frame->Mute();
    OnAudioFrameCallback sink;
    {std::lock_guard<std::mutex> lock(mutex_);sink=sink_;}
    if(sink)sink(std::move(frame));
  }
  void SetSink(OnAudioFrameCallback sink) override {
    std::lock_guard<std::mutex> lock(mutex_);sink_=std::move(sink);
  }
 private:
  MuteGate gate_;std::mutex mutex_;OnAudioFrameCallback sink_;
};
class SineDevice : public webrtc::webrtc_impl::AudioDeviceModuleDefault<webrtc::AudioDeviceModule> {
 public:
  ~SineDevice() override { Terminate(); }
  int32_t RegisterAudioCallback(webrtc::AudioTransport* t) override { transport_=t;return 0; }
  int32_t StartRecording() override { recording_=true;start();return 0; }
  int32_t StartPlayout() override { playing_=true;start();return 0; }
  int32_t StopRecording() override {recording_=false;return 0;}
  int32_t StopPlayout() override {playing_=false;return 0;}
  bool Recording() const override {return recording_;}
  bool Playing() const override {return playing_;}
  int32_t Terminate() override {running_=false;if(thread_.joinable())thread_.join();return 0;}
  Json stats() const {return {{"frames",frames_.load()},{"signal_frames",signal_frames_.load()},{"rms",rms_.load()},{"clipped",clipped_.load()},{"frequency_hz",frequency_.load()}};}
 private:
  void start(){if(thread_.joinable())return;running_=true;thread_=std::thread([this]{run();});}
  void run(){
    uint64_t sample=0;auto next=std::chrono::steady_clock::now();
    int crossings=0, measured=0; int16_t previous=0;
    while(running_){
      next+=10ms;int16_t buffer[480]{};auto* t=transport_.load();
      if(t&&recording_){
        for(auto& s:buffer)s=static_cast<int16_t>(3000*std::sin(6.283185307179586*440*double(sample++)/48000));
        uint32_t level=0;t->RecordedDataIsAvailable(buffer,480,2,1,48000,0,0,0,false,level);
      }
      if(t&&playing_){
        size_t out=0;int64_t elapsed=0,ntp=0;t->NeedMorePlayData(480,2,1,48000,buffer,out,&elapsed,&ntp);
        double energy=0;out=std::min(out,size_t(480));
        for(size_t i=0;i<out;++i){
          energy+=double(buffer[i])*buffer[i];
          if(buffer[i]==INT16_MAX||buffer[i]==INT16_MIN)++clipped_;
          if(previous<=0&&buffer[i]>0)++crossings;
          previous=buffer[i];
        }
        rms_=out ? int(std::sqrt(energy/out)) : 0;
        if(rms_>100)++signal_frames_;
        ++frames_;measured+=int(out);
        if(measured>=48000){frequency_=double(crossings)*48000/measured;crossings=0;measured=0;}
      }
      std::this_thread::sleep_until(next);
    }
  }
  std::atomic<webrtc::AudioTransport*> transport_{nullptr};
  std::atomic<bool> running_{false},playing_{false},recording_{false};
  std::atomic<int> frames_{0},signal_frames_{0},rms_{0},clipped_{0};
  std::atomic<double> frequency_{0};std::thread thread_;
};
template<class T> T wait(std::future<T> future) {
  if(future.wait_for(8s)!=std::future_status::ready)throw std::runtime_error("WebRTC 操作超时");
  return future.get();
}
class CreateObserver : public webrtc::CreateSessionDescriptionObserver {
 public:
  std::promise<std::unique_ptr<webrtc::SessionDescriptionInterface>> promise;
  void OnSuccess(webrtc::SessionDescriptionInterface* d) override {promise.set_value(std::unique_ptr<webrtc::SessionDescriptionInterface>(d));}
  void OnFailure(webrtc::RTCError) override {promise.set_value(nullptr);}
};
class SetObserver : public webrtc::SetSessionDescriptionObserver {
 public:
  std::promise<bool> promise;
  void OnSuccess() override {promise.set_value(true);}
  void OnFailure(webrtc::RTCError) override {promise.set_value(false);}
};
class StatsObserver : public webrtc::RTCStatsCollectorCallback {
 public:
  std::promise<webrtc::scoped_refptr<const webrtc::RTCStatsReport>> promise;
  void OnStatsDelivered(const webrtc::scoped_refptr<const webrtc::RTCStatsReport>& r) override {promise.set_value(r);}
};
class Peer : public webrtc::PeerConnectionObserver {
 public:
  Peer(std::string name,Media::Event event):name_(std::move(name)),event_(std::move(event)){}
  void OnSignalingChange(webrtc::PeerConnectionInterface::SignalingState) override {}
  void OnDataChannel(webrtc::scoped_refptr<webrtc::DataChannelInterface>) override {}
  void OnIceCandidate(const webrtc::IceCandidate* candidate) override {
    // 只有 P2P 需要逐条交换候选；SFU 由服务端从连通性检查学习反射地址。
    if(!trickle_||!candidate)return;
    event_("p2p.candidate",{{"candidate",candidate->ToString()},{"sdp_mid",candidate->sdp_mid()},
                            {"sdp_mline_index",candidate->sdp_mline_index()}});
  }
  void OnIceGatheringChange(webrtc::PeerConnectionInterface::IceGatheringState) override {}
  void OnConnectionChange(webrtc::PeerConnectionInterface::PeerConnectionState state) override {
    event_("media.state",{{"pc",name_},{"state",static_cast<int>(state)}});
  }
  void OnTrack(webrtc::scoped_refptr<webrtc::RtpTransceiverInterface> t) override {
    auto track=t->receiver()->track();
    if(track&&track->kind()=="audio")static_cast<webrtc::AudioTrackInterface*>(track.get())->GetSource()->SetVolume(gain.load());
  }
  std::atomic<double> gain{0.5};
  bool trickle_=false;
  webrtc::scoped_refptr<webrtc::PeerConnectionInterface> pc;
 private:
  std::string name_;Media::Event event_;
};
class WebRtcMedia : public Media {
 public:
  WebRtcMedia(const Json& config,const Json& ice,MuteGate gate,Event event)
      : config_(config),event_(std::move(event)),send_("send",event_),recv_("recv",event_),p2p_("p2p",event_) {
    p2p_.trickle_=true;
    // 默认不输出 WebRTC 的 SDP/候选日志，避免凭证进入诊断输出。
    webrtc::LogMessage::LogToDebug(webrtc::LS_NONE);
    network_=webrtc::Thread::CreateWithSocketServer();worker_=webrtc::Thread::Create();signaling_=webrtc::Thread::Create();
    network_->SetName("talk-network",nullptr);worker_->SetName("talk-worker",nullptr);signaling_->SetName("talk-signaling",nullptr);
    if(!network_->Start()||!worker_->Start()||!signaling_->Start())throw std::runtime_error("线程启动失败");
    const auto backend=config.value("audio_backend","none");
    auto env=webrtc::CreateEnvironment();
    if(backend=="fake"){sine_=webrtc::make_ref_counted<SineDevice>();adm_=sine_;}
    else if(backend=="alsa"||backend=="pulse") {
      adm_=worker_->BlockingCall([&]{return webrtc::CreateAudioDeviceModule(env,backend=="alsa" ? webrtc::AudioDeviceModule::kLinuxAlsaAudio : webrtc::AudioDeviceModule::kLinuxPulseAudio);});
    }else throw std::runtime_error("未启用音频后端");
    if(!adm_)throw std::runtime_error("ADM 不可用");
    if(worker_->BlockingCall([&]{return adm_->Init();})!=0)throw std::runtime_error("ADM 初始化失败");
    webrtc::PeerConnectionFactoryDependencies deps;
    deps.network_thread=network_.get();deps.worker_thread=worker_.get();deps.signaling_thread=signaling_.get();deps.env=env;deps.adm=adm_;
    webrtc::AudioProcessing::Config apm;
    apm.echo_canceller.enabled=backend!="fake";
    apm.noise_suppression.enabled=backend!="fake";
    apm.gain_controller2.enabled=backend!="fake";
    apm.high_pass_filter.enabled=true;
    // 合成内容检测不经过 APM，避免默认音频选项重新启用 AEC 而抵消测试正弦。
    if(backend!="fake")deps.audio_processing_builder=std::make_unique<webrtc::BuiltinAudioProcessingBuilder>(apm);
    deps.audio_encoder_factory=webrtc::CreateAudioEncoderFactory<webrtc::AudioEncoderOpus>();
    deps.audio_decoder_factory=webrtc::CreateAudioDecoderFactory<webrtc::AudioDecoderOpus>();
    deps.audio_frame_processor=std::make_unique<CaptureGate>(std::move(gate));
    webrtc::EnableMedia(deps);
    factory_=webrtc::CreateModularPeerConnectionFactory(std::move(deps));
    if(!factory_)throw std::runtime_error("Factory 不可用");
    rtc_config_.sdp_semantics=webrtc::SdpSemantics::kUnifiedPlan;
    for(const auto& item:ice){
      webrtc::PeerConnectionInterface::IceServer server;
      if(item.at("urls").is_array())server.urls=item.at("urls").get<std::vector<std::string>>();
      else server.urls={item.at("urls").get<std::string>()};
      server.username=item.value("username","");server.password=item.value("credential","");
      rtc_config_.servers.push_back(server);
    }
  }
  ~WebRtcMedia() override {
    if(send_.pc)send_.pc->Close();
    if(recv_.pc)recv_.pc->Close();
    if(p2p_.pc)p2p_.pc->Close();
    send_.pc=nullptr;recv_.pc=nullptr;p2p_.pc=nullptr;sender_=nullptr;p2p_sender_=nullptr;track_=nullptr;factory_=nullptr;
    if(adm_)worker_->BlockingCall([&]{adm_->StopRecording();adm_->StopPlayout();adm_->Terminate();});
    adm_=nullptr;sine_=nullptr;
  }
  Json offer() override {
    select_devices();ensure_track();create(send_);
    if(!sender_){
      webrtc::RtpTransceiverInit init;init.direction=webrtc::RtpTransceiverDirection::kSendOnly;init.stream_ids={"talk"};
      auto r=send_.pc->AddTransceiver(track_,init);if(!r.ok())throw std::runtime_error("添加音轨失败");sender_=r.value()->sender();
      apply_sender(sender_,sfu_send_&&!muted_);
    }
    // 不在此重置静音：初次发布由构造默认静音，切回 SFU 时保留用户已开麦状态。
    set_local(send_,true);
    const auto transceivers=send_.pc->GetTransceivers();
    const auto mid=transceivers.front()->mid().value_or("0");
    return {{"sdp",local(send_)},{"tracks",Json::array({{{"mid",mid},{"kind","audio"},{"source","mic"}}})}};
  }
  void accept(const std::string& sdp) override {set_remote(send_,webrtc::SdpType::kAnswer,sdp);}
  Json answer(const std::string& sdp) override {select_devices();create(recv_);set_remote(recv_,webrtc::SdpType::kOffer,sdp);set_local(recv_,false);apply_gain(recv_);return {{"sdp",local(recv_)}};}
  void mute(bool muted) override {
    muted_=muted;
    if(track_)track_->set_enabled(!muted);
    apply_sender(sender_,sfu_send_&&!muted);
    apply_sender(p2p_sender_,!muted);
  }
  Json devices() override {
    return worker_->BlockingCall([&]{
      Json out={{"recording",Json::array()},{"playout",Json::array()}};
      for(bool recording:{true,false}){
        const int count=recording?adm_->RecordingDevices():adm_->PlayoutDevices();
        for(int i=0;i<count;++i){
          char name[webrtc::kAdmMaxDeviceNameSize]{},guid[webrtc::kAdmMaxGuidSize]{};
          const int status=recording?adm_->RecordingDeviceName(i,name,guid):adm_->PlayoutDeviceName(i,name,guid);
          if(status==0)out[recording?"recording":"playout"].push_back({{"index",i},{"name",name},{"id",*guid?guid:name}});
        }
      }
      return out;
    });
  }
  void volume(double gain) override {
    user_gain_=gain;apply_gain(recv_);apply_gain(p2p_);
  }
  Json stats() override {
    Json out=Json::object();
    for(auto* peer:{&send_,&recv_,&p2p_}){
      if(!peer->pc)continue;
      auto observer=webrtc::make_ref_counted<StatsObserver>();auto future=observer->promise.get_future();peer->pc->GetStats(observer.get());auto report=wait(std::move(future));
      uint64_t received=0,sent=0;int64_t lost=0;double jitter=0;
      for(const auto* s:report->GetStatsOfType<webrtc::RTCInboundRtpStreamStats>())if(s->kind.value_or("")=="audio"){
        received+=s->packets_received.value_or(0);lost+=s->packets_lost.value_or(0);jitter=std::max(jitter,s->jitter.value_or(0));
      }
      for(const auto* s:report->GetStatsOfType<webrtc::RTCOutboundRtpStreamStats>())if(s->kind.value_or("")=="audio")sent+=s->packets_sent.value_or(0);
      const auto key=peer==&send_?"send":peer==&recv_?"recv":"p2p";
      out[key]={{"received",received},{"sent",sent},{"lost",lost},{"jitter_seconds",jitter}};
      if(peer==&p2p_){
        const auto path=candidate_path(*report);
        if(!path.empty())out[key]["path"]=path;
      }
    }
    if(sine_)out["fake_playout"]=sine_->stats();
    return out;
  }
  /* ---- P2P ---- */
  Json p2p_offer() override {
    select_devices();ensure_track();create(p2p_);
    add_p2p_sender();
    set_local(p2p_,true);
    return {{"sdp",local(p2p_)}};
  }
  void p2p_accept(const std::string& sdp) override {
    if(!p2p_.pc)throw std::runtime_error("P2P PC 尚未建立");
    set_remote(p2p_,webrtc::SdpType::kAnswer,sdp);
    apply_gain(p2p_);
  }
  Json p2p_answer(const std::string& sdp) override {
    select_devices();ensure_track();create(p2p_);
    add_p2p_sender();
    set_remote(p2p_,webrtc::SdpType::kOffer,sdp);set_local(p2p_,false);apply_gain(p2p_);
    return {{"sdp",local(p2p_)}};
  }
  void p2p_candidate(const std::string& candidate,const std::string& mid,int index) override {
    if(candidate.empty())return;
    if(!p2p_.pc)throw std::runtime_error("P2P PC 尚未建立");
    auto parsed=webrtc::IceCandidate::Create(mid,index,candidate);
    if(!parsed||!p2p_.pc->AddIceCandidate(parsed.get()))throw std::runtime_error("远端 P2P 候选无效");
  }
  void p2p_close() override {
    if(p2p_.pc)p2p_.pc->Close();
    p2p_.pc=nullptr;p2p_sender_=nullptr;
  }
  Json p2p_path() override {
    Json out=Json::object();
    if(!p2p_.pc)return out;
    auto observer=webrtc::make_ref_counted<StatsObserver>();auto future=observer->promise.get_future();p2p_.pc->GetStats(observer.get());auto report=wait(std::move(future));
    if(!report)return out;
    const auto path=candidate_path(*report);
    if(!path.empty())out["path"]=path;
    return out;
  }
  /* ---- 模式切换 ---- */
  void sfu_send(bool enabled) override {
    sfu_send_=enabled;
    apply_sender(sender_,enabled&&!muted_);
  }
  void sfu_playback(bool enabled) override {
    playback_=enabled;
    apply_gain(recv_);
  }
  void update_ice(const Json& ice) override {
    rtc_config_.servers.clear();
    for(const auto& item:ice){
      webrtc::PeerConnectionInterface::IceServer server;
      if(item.at("urls").is_array())server.urls=item.at("urls").get<std::vector<std::string>>();
      else server.urls={item.at("urls").get<std::string>()};
      server.username=item.value("username","");server.password=item.value("credential","");
      rtc_config_.servers.push_back(server);
    }
    // 新凭证在下次 ICE restart/新建 PC 生效；已有 PC 尝试热更新，失败不阻断。
    for(auto* peer:{&send_,&recv_,&p2p_})if(peer->pc)(void)peer->pc->SetConfiguration(rtc_config_);
  }
 private:
  void ensure_track(){
    if(track_)return;
    auto source=factory_->CreateAudioSource(webrtc::AudioOptions());track_=factory_->CreateAudioTrack("mic",source.get());track_->set_enabled(false);
  }
  void add_p2p_sender(){
    if(p2p_sender_){apply_sender(p2p_sender_,!muted_);return;}
    webrtc::RtpTransceiverInit init;init.direction=webrtc::RtpTransceiverDirection::kSendRecv;init.stream_ids={"talk"};
    auto r=p2p_.pc->AddTransceiver(track_,init);if(!r.ok())throw std::runtime_error("添加 P2P 音轨失败");
    p2p_sender_=r.value()->sender();
    apply_sender(p2p_sender_,!muted_);
  }
  void apply_sender(const webrtc::scoped_refptr<webrtc::RtpSenderInterface>& sender,bool enabled){
    if(!sender)return;
    auto p=sender->GetParameters();for(auto& e:p.encodings){e.active=enabled;e.max_bitrate_bps=32000;}
    if(!sender->SetParameters(p).ok()){if(track_)track_->set_enabled(false);throw std::runtime_error("设置发送状态失败");}
  }
  void apply_gain(Peer& peer){
    peer.gain=(&peer==&recv_&&!playback_)?0.0:user_gain_;
    if(!peer.pc)return;
    for(auto& receiver:peer.pc->GetReceivers()){
      auto track=receiver->track();if(track&&track->kind()=="audio")static_cast<webrtc::AudioTrackInterface*>(track.get())->GetSource()->SetVolume(peer.gain.load());
    }
  }
  /* 从选中且已提名的候选对判定实际路径；relay 优先于 srflx。 */
  static std::string candidate_path(const webrtc::RTCStatsReport& report){
    std::map<std::string,std::string> types;
    for(const auto* c:report.GetStatsOfType<webrtc::RTCLocalIceCandidateStats>())if(c->candidate_type)types[c->id()]=*c->candidate_type;
    for(const auto* c:report.GetStatsOfType<webrtc::RTCRemoteIceCandidateStats>())if(c->candidate_type)types[c->id()]=*c->candidate_type;
    for(const auto* pair:report.GetStatsOfType<webrtc::RTCIceCandidatePairStats>()){
      if(!pair->state||*pair->state!="succeeded"||!pair->nominated.value_or(false))continue;
      if(!pair->local_candidate_id||!pair->remote_candidate_id)continue;
      const auto local=types.find(*pair->local_candidate_id);const auto remote=types.find(*pair->remote_candidate_id);
      if(local==types.end()||remote==types.end())continue;
      if(local->second=="relay"||remote->second=="relay")return "relay";
      if(local->second=="srflx"||remote->second=="srflx"||local->second=="prflx"||remote->second=="prflx")return "srflx";
      return "host";
    }
    return "";
  }
  void create(Peer& peer){
    if(peer.pc)return;
    auto r=factory_->CreatePeerConnectionOrError(rtc_config_,webrtc::PeerConnectionDependencies(&peer));
    if(!r.ok())throw std::runtime_error("创建 PC 失败");
    peer.pc=r.MoveValue();
  }
  void select_devices(){
    if(selected_)return;
    if(sine_){selected_=true;return;}
    auto list=devices();
    for(bool recording:{true,false}){
      const auto key=recording?"recording_device":"playout_device";
      const auto wanted=config_.value(key,"");if(wanted.empty())throw std::runtime_error("需要显式选择音频设备");
      int index=-1;for(const auto& d:list[recording?"recording":"playout"])if(d["id"]==wanted){index=d["index"].get<int>();break;}
      if(index<0)throw std::runtime_error("音频设备不存在");
      if(worker_->BlockingCall([&]{return recording?adm_->SetRecordingDevice(uint16_t(index)):adm_->SetPlayoutDevice(uint16_t(index));})!=0)throw std::runtime_error("设备不可用或被占用");
    }
    selected_=true;
  }
  static std::string local(Peer& p){std::string s;if(!p.pc->local_description()->ToString(&s))throw std::runtime_error("SDP 序列化失败");return s;}
  static void set_local(Peer& p,bool offer){
    auto c=webrtc::make_ref_counted<CreateObserver>();auto f=c->promise.get_future();
    webrtc::PeerConnectionInterface::RTCOfferAnswerOptions options;
    if(offer)p.pc->CreateOffer(c.get(),options);else p.pc->CreateAnswer(c.get(),options);
    auto description=wait(std::move(f));if(!description)throw std::runtime_error("SDP 创建失败");
    auto s=webrtc::make_ref_counted<SetObserver>();auto sf=s->promise.get_future();p.pc->SetLocalDescription(s.get(),description.release());
    if(!wait(std::move(sf)))throw std::runtime_error("设置本地 SDP 失败");
  }
  static void set_remote(Peer& p,webrtc::SdpType type,const std::string& sdp){
    if(!p.pc)throw std::runtime_error("PC 尚未初始化");
    auto d=webrtc::CreateSessionDescription(type,sdp);
    if(!d)throw std::runtime_error("远端 SDP 无效");
    auto s=webrtc::make_ref_counted<SetObserver>();auto f=s->promise.get_future();p.pc->SetRemoteDescription(s.get(),d.release());
    if(!wait(std::move(f)))throw std::runtime_error("设置远端 SDP 失败");
  }
  Json config_;Event event_;
  std::unique_ptr<webrtc::Thread> network_,worker_,signaling_;
  webrtc::scoped_refptr<webrtc::AudioDeviceModule> adm_;
  webrtc::scoped_refptr<SineDevice> sine_;
  webrtc::scoped_refptr<webrtc::PeerConnectionFactoryInterface> factory_;
  webrtc::PeerConnectionInterface::RTCConfiguration rtc_config_;
  Peer send_,recv_,p2p_;
  webrtc::scoped_refptr<webrtc::AudioTrackInterface> track_;
  webrtc::scoped_refptr<webrtc::RtpSenderInterface> sender_,p2p_sender_;
  bool selected_=false,muted_=true,sfu_send_=true,playback_=true;
  double user_gain_=0.5;
};
}
std::unique_ptr<Media> make_media(const Json& config,const Json& ice,MuteGate gate,Media::Event event){return std::make_unique<WebRtcMedia>(config,ice,std::move(gate),std::move(event));}
}
