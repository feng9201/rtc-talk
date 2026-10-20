#include "talk/talk.h"
#include "signaling/protocol.h"
#include "media/media.h"
#include <websocketpp/config/asio_no_tls_client.hpp>
#include <websocketpp/client.hpp>
#include <asio.hpp>
#include <atomic>
#include <chrono>
#include <future>
#include <map>
#include <random>
#include <thread>

namespace talk {
using Ws = websocketpp::client<websocketpp::config::asio_client>;
using Done = std::function<void(Json)>;
using namespace std::chrono_literals;
class Core {
 public:
  Core(const talk_options& options, Json config)
      : config_(std::move(config)), callback_(options.on_event), user_(options.userdata),
        guard_(asio::make_work_guard(io_)), heartbeat_(io_), reconnect_(io_), media_pool_(1) {
    ws_.clear_access_channels(websocketpp::log::alevel::all);
    ws_.clear_error_channels(websocketpp::log::elevel::all);
    ws_.init_asio(&io_);
    ws_.set_max_message_size(kMaxMessage);
    thread_ = std::thread([this] { io_.run(); });
  }
  ~Core() {
    std::promise<void> barrier;
    asio::post(io_, [this, &barrier] {
      stopping_ = true; retry_ = false; ++epoch_;
      mute_gate_->store(true);media_valid_->store(false);
      heartbeat_.cancel(); reconnect_.cancel();
      for (auto& p : pending_) p.second.timer->cancel();
      pending_.clear();
      if (connection_) { websocketpp::lib::error_code ec; connection_->terminate(ec); }
      barrier.set_value();
    });
    barrier.get_future().wait();
    asio::post(media_pool_, [this] { media_.reset(); });
    media_pool_.join();
    guard_.reset(); io_.stop();
    thread_.join();
  }
  uint64_t submit(std::string method, Json data) {
    if (queued_.fetch_add(1) >= 256) { --queued_; return 0; }
    auto id = next_operation_.fetch_add(1);
    asio::post(io_, [this, id, method=std::move(method), data=std::move(data)]() mutable {
      --queued_;
      try { command(id, method, std::move(data)); }
      catch (...) { result(id, method, failure("", 400, "参数或状态无效")); }
    });
    return id;
  }
 private:
  struct Pending { std::shared_ptr<asio::steady_timer> timer; Done done; };
  void emit(const std::string& name, const Json& data) {
    if (stopping_ || !callback_) return;
    const auto text = data.dump();
    try { callback_(user_, name.c_str(), text.c_str()); } catch (...) { /* C 边界不传播异常。 */ }
  }
  void result(uint64_t id, const std::string& method, Json r) {
    r.erase("v"); r.erase("type"); r["id"] = id; r["method"] = method;
    if (r.contains("error")) r["error"] = {{"code",r["error"].value("code",500)},{"msg","请求未成功"}};
    emit("result", r);
  }
  bool send(const Json& j) {
    if (!open_ || !connection_) return false;
    auto text = j.dump();
    if (text.size() > kMaxMessage || connection_->get_buffered_amount() > 2*kMaxMessage) return false;
    const auto ec = connection_->send(text, websocketpp::frame::opcode::text);
    return !ec;
  }
  void request(const std::string& method, const Json& data, Done done) {
    if (!open_ || pending_.size() >= 128) { done(failure("",480,"连接不可用")); return; }
    const auto id = "c-" + std::to_string(++next_request_);
    auto timer = std::make_shared<asio::steady_timer>(io_);
    timer->expires_after(method.rfind("sfu.",0)==0 ? 15s : 10s);
    pending_.emplace(id, Pending{timer,std::move(done)});
    timer->async_wait([this,id](const asio::error_code& ec) {
      auto it = pending_.find(id);
      if (ec || it == pending_.end()) return;
      auto callback = std::move(it->second.done); pending_.erase(it);
      callback(failure(id,504,"请求超时"));
    });
    if (!send({{"v",1},{"type","req"},{"id",id},{"method",method},{"data",data}})) {
      timer->cancel(); auto callback=std::move(pending_.at(id).done); pending_.erase(id);
      callback(failure(id,480,"发送失败"));
    }
  }
  void start_connect(uint64_t operation, bool recovery) {
    reconnect_.cancel();
    if(recovery && std::chrono::steady_clock::now()>resume_deadline_){session_.clear();reset_media();room_.clear();}
    const auto epoch = ++epoch_;
    ready_=false; open_=false;
    websocketpp::lib::error_code ec;
    connection_ = ws_.get_connection(url_, ec);
    if (ec) { retry_=false; result(operation,"connect",failure("",400,"地址无效")); return; }
    connection_->add_subprotocol("talk.v1.json");
    connection_->append_header("Authorization","Bearer "+token_);
    connection_->set_open_handshake_timeout(8000);
    connection_->set_open_handler([this,epoch,operation,recovery](auto) {
      if (epoch != epoch_ || stopping_) return;
      open_=true;
      Json hello={{"client",{{"platform","windows-x64"},{"version",talk_version()},{"sdk","webrtc-m146"}}},
                  {"caps",{{"audio",{{"codecs",Json::array({"opus"})},{"aec","webrtc"}}},{"p2p",false},{"sfu_sdp","unified-plan"}}}};
      if (!session_.empty()) hello["resume"]={{"session_id",session_}};
      request("session.hello",hello,[this,epoch,operation,recovery](Json r) {
        if (epoch!=epoch_) { result(operation,"connect",failure("",487,"连接已取消")); return; }
        if (!r.value("ok",false)) { result(operation,"connect",r); lost(epoch); return; }
        auto d=r.value("data",Json::object());
        if (!d.contains("session_id") || !d["session_id"].is_string()) { lost(epoch); return; }
        session_=d["session_id"].get<std::string>(); ice_=d.value("ice_servers",Json::array());
        ready_=true; retry_count_=0;
        const bool resumed=d.value("resumed",false);
        heartbeat_seconds_=std::max(1,std::min(30,d.value("heartbeat_interval",15)));
        resume_seconds_=std::max(1,std::min(3600,d.value("resume_ttl",60)));
        if (!resumed) { reset_media(); room_.clear(); }
        auto public_data=d; public_data.erase("ice_servers"); public_data["recovering"]=recovery;
        result(operation,"connect",response("",public_data)); emit("ready",public_data); ping_later();
        if (recovery && resumed && d.contains("room")) {
          room_=d["room"]["room"].value("room_id",""); emit("joined",d["room"]);
          command(0,"media.setMute",{{"source","mic"},{"muted",true}});
        } else if (recovery && !join_intent_.empty()) command(0,"room.join",join_intent_);
      });
    });
    connection_->set_message_handler([this,epoch](auto,Ws::message_ptr message) {
      if (epoch!=epoch_ || stopping_) return;
      try { incoming(parse_message(message->get_payload())); }
      catch (...) { emit("error",{{"code",400},{"msg","收到无效信令"}}); }
    });
    connection_->set_close_handler([this,epoch](auto) {
      if (epoch!=epoch_) return;
      const auto code=connection_->get_remote_close_code();
      if (code==4001 || code==4003) retry_=false;
      lost(epoch);
    });
    connection_->set_fail_handler([this,epoch,operation](auto) {
      if (epoch!=epoch_) return;
      const auto status=connection_->get_response_code();
      if (status==401 || status==403 || status==404) retry_=false;
      result(operation,"connect",failure("",status ? static_cast<int>(status) : 480,"握手失败"));
      lost(epoch);
    });
    emit("state",{{"state",recovery ? "reconnecting" : "connecting"}});
    ws_.connect(connection_);
  }
  void ping_later() {
    heartbeat_.expires_after(std::chrono::seconds(heartbeat_seconds_));
    heartbeat_.async_wait([this](const asio::error_code& ec) {
      if (ec || !ready_) return;
      const auto epoch=epoch_;
      request("session.ping",Json::object(),[this,epoch](Json r) {
        if (epoch!=epoch_) return;
        if (!r.value("ok",false)) lost(epoch); else ping_later();
      });
    });
  }
  void lost(uint64_t epoch) {
    if (epoch!=epoch_ || stopping_) return;
    if(ready_)resume_deadline_=std::chrono::steady_clock::now()+std::chrono::seconds(resume_seconds_);
    mute_gate_->store(true);
    ++epoch_; ++mute_epoch_; ready_=false; open_=false; joining_=false; disconnecting_=false;
    heartbeat_.cancel(); reconnect_.cancel();
    if (connection_) { websocketpp::lib::error_code ec; connection_->terminate(ec); }
    auto pending=std::move(pending_); pending_.clear();
    for (auto& p:pending) { p.second.timer->cancel(); p.second.done(failure(p.first,480,"连接断开")); }
    media_job([](Media& m){m.mute(true); return Json::object();},[](Json){},false);
    emit("state",{{"state",retry_ ? "reconnecting" : "disconnected"}});
    if (!retry_) { session_.clear(); room_.clear(); join_intent_=Json::object(); reset_media(); return; }
    const int seconds=std::min(15,1<<std::min(retry_count_++,4));
    reconnect_.expires_after(std::chrono::milliseconds(seconds*1000+int(random_()%250)));
    reconnect_.async_wait([this](const asio::error_code& ec){ if(!ec && retry_) start_connect(0,true); });
  }
  void reset_media() {
    mute_gate_->store(true);media_valid_->store(false);
    mute_gate_=std::make_shared<std::atomic<bool>>(true);
    media_valid_=std::make_shared<std::atomic<bool>>(true);
    recv_consumers_=Json::object();
    ++media_epoch_; ++mute_epoch_;
    asio::post(media_pool_,[this]{media_.reset();});
  }
  void media_job(std::function<Json(Media&)> job, Done done, bool create=true) {
    if (media_queued_ >= 64) { done(failure("",429,"媒体队列已满")); return; }
    ++media_queued_;
    const auto generation=media_epoch_;
    const auto config=config_; const auto ice=ice_;const auto gate=mute_gate_;const auto valid=media_valid_;
    asio::post(media_pool_,[this,generation,config,ice,gate,valid,job=std::move(job),done=std::move(done),create]() mutable {
      Json r;
      try {
        if(!valid->load())throw std::runtime_error("媒体任务已取消");
        if (!media_ && create) media_=make_media(config,ice,gate,[this,generation](std::string event,Json data) {
          asio::post(io_,[this,generation,event=std::move(event),data=std::move(data)] {
            if(generation==media_epoch_) emit(event,data);
          });
        });
        if(!media_){
          if(create)throw std::runtime_error("媒体初始化失败");
          r=response("",Json::object());
        }else{
          r=response("",job(*media_));
        }
      } catch (...) { r=failure("",500,"媒体操作失败，请检查设备或 SDP 兼容性"); }
      asio::post(io_,[this,generation,done=std::move(done),r=std::move(r)]() mutable {
        --media_queued_;
        if (!stopping_) done(generation==media_epoch_ ? std::move(r) : failure("",487,"媒体操作已取消"));
      });
    });
  }
  void publish() {
    const auto epoch=epoch_,generation=media_epoch_;
    media_job([](Media& m){return m.offer();},[this,epoch,generation](Json r) {
      if(epoch!=epoch_||generation!=media_epoch_) return;
      if(!r.value("ok",false)){emit("media.error",{{"msg","生成 offer 失败"}});return;}
      request("sfu.publish",r["data"],[this,epoch,generation](Json a){
        if(epoch!=epoch_||generation!=media_epoch_) return;
        if(!a.value("ok",false)){emit("media.error",{{"msg","发布失败"},{"code",a.value("error",Json::object()).value("code",500)}});return;}
        const auto sdp=a["data"].value("sdp","");
        media_job([sdp](Media& m){m.accept(sdp);return Json::object();},[this,epoch,generation](Json ok){
          if(epoch!=epoch_||generation!=media_epoch_)return;
          emit(ok.value("ok",false)?"published":"media.error",{{"ok",ok.value("ok",false)}});
        });
      });
    });
  }
  void incoming(Json j) {
    const auto type=j["type"].get<std::string>();
    if(type=="res") {
      auto it=pending_.find(j["id"].get<std::string>()); if(it==pending_.end())return;
      it->second.timer->cancel(); auto done=std::move(it->second.done); pending_.erase(it); done(std::move(j)); return;
    }
    const auto method=j["method"].get<std::string>(); auto data=j.value("data",Json::object());
    if(type=="req") {
      const auto id=j["id"].get<std::string>(); const auto epoch=epoch_;
      if(method!="sfu.offer" || config_.value("audio_backend","none")=="none") {
        send(failure(id,501,"该客户端尚不支持此请求")); return;
      }
      if (!data.is_object() || !data.contains("sdp") || !data["sdp"].is_string()) {
        send(failure(id,400,"缺少 SDP")); return;
      }
      if(room_.empty()&&!joining_){send(failure(id,409,"未入房"));return;}
      const auto sdp=data.at("sdp").get<std::string>();
      const auto consumers=data.value("consumers",Json::array());
      media_job([sdp](Media& m){return m.answer(sdp);},[this,id,epoch,consumers](Json r){
        if(epoch!=epoch_)return;
        r["id"]=id;send(r);
        if(r.value("ok",false)){
          recv_consumers_=Json::object();
          for(const auto& c:consumers)recv_consumers_[c.at("mid").get<std::string>()]=c;
        }
        emit("subscribed",{{"ok",r.value("ok",false)},{"consumers",recv_consumers_}});
      }); return;
    }
    if(method=="session.kicked") { retry_=false; emit(method,data); lost(epoch_); return; }
    if(method=="room.closed") { room_.clear();join_intent_=Json::object();reset_media(); }
    if(method=="media.forceMute" || method=="floor.revoked") {
      mute_gate_->store(true);
      ++mute_epoch_; media_job([](Media& m){m.mute(true);return Json::object();},[](Json){},false);
    }
    emit(method,data);
  }
  void command(uint64_t id,const std::string& method,Json data) {
    if(stopping_)return;
    if(method=="auth.update") {
      const auto token=data.at("token").get<std::string>();
      if(token.empty()||token.find_first_of("\r\n")!=std::string::npos)throw std::runtime_error("token 无效");
      token_=token;result(id,method,response("",Json::object()));return;
    }
    if(method=="connect") {
      if(retry_ || ready_ || disconnecting_) {result(id,method,failure("",409,"已经连接或正在退出"));return;}
      url_=data.at("url").get<std::string>(); token_=data.at("token").get<std::string>();
      if(url_.rfind("ws://",0)!=0 || url_.find('?')!=std::string::npos || token_.empty() || token_.find_first_of("\r\n")!=std::string::npos)
        throw std::runtime_error("连接参数无效");
      retry_=true; start_connect(id,false); return;
    }
    if(method=="disconnect" || method=="session.bye") {
      if(disconnecting_){result(id,method,failure("",409,"正在退出"));return;}
      retry_=false; reconnect_.cancel(); reset_media();
      if(!ready_){lost(epoch_);result(id,method,response("",Json::object()));return;}
      disconnecting_=true; const auto epoch=epoch_;
      request("session.bye",Json::object(),[this,id,method,epoch](Json r){
        lost(epoch); result(id,method,r);
      });return;
    }
    if(disconnecting_){result(id,method,failure("",409,"正在退出"));return;}
    if(method=="media.stats" || method=="audio.devices" || method=="audio.volume") {
      media_job([method,data](Media& m){
        if(method=="media.stats")return m.stats();
        if(method=="audio.devices")return m.devices();
        const double gain=data.at("gain").get<double>();if(!std::isfinite(gain)||gain<0||gain>1)throw std::runtime_error("音量越界");
        m.volume(gain);return Json::object();
      },[this,id,method](Json r){result(id,method,r);});return;
    }
    if(!ready_){result(id,method,failure("",409,"尚未完成 hello"));return;}
    if(method=="room.join") {
      if(!room_.empty() || joining_){result(id,method,failure("",409,"已经入房"));return;}
      data["media"]={{"audio",{{"send",true},{"muted",true}}},{"video",{{"send",false}}}};
      data["policy"]={{"talk_mode","free"},{"media_mode","sfu"}};
      joining_=true; const auto epoch=epoch_;
      request(method,data,[this,id,method,data,epoch](Json r){
        joining_=false;
        if(epoch!=epoch_){result(id,method,failure("",487,"入房已取消"));return;}
        if(r.value("ok",false)) {
          join_intent_=data;room_=data.at("room_id").get<std::string>(); emit("joined",r["data"]);
          if(config_.value("audio_backend","none")!="none")publish();
        }
        result(id,method,r);
      });return;
    }
    if(method=="room.leave") {
      if(room_.empty()){result(id,method,failure("",409,"未入房"));return;}
      data={{"room_id",room_}};join_intent_=Json::object();reset_media();
      request(method,data,[this,id,method](Json r){if(r.value("ok",false)){room_.clear();emit("left",Json::object());} result(id,method,r);});return;
    }
    if(method=="media.setMute") {
      if(room_.empty()){result(id,method,failure("",409,"未入房"));return;}
      const bool muted=data.at("muted").get<bool>();
      const auto generation=++mute_epoch_;
      if(muted)mute_gate_->store(true);
      if(muted)media_job([](Media& m){m.mute(true);return Json::object();},[](Json){},false);
      request(method,{{"source","mic"},{"muted",muted}},[this,id,method,muted,generation](Json r){
        if(generation!=mute_epoch_){result(id,method,failure("",487,"静音命令已被更新"));return;}
        const bool effective=muted || !r.value("ok",false);
        media_job([effective](Media& m){m.mute(effective);return Json::object();},
          [this,id,method,generation,effective,r=std::move(r)](Json applied) mutable {
            if(generation!=mute_epoch_){result(id,method,failure("",487,"静音命令已被更新"));return;}
            if(!applied.value("ok",false))r=std::move(applied);
            mute_gate_->store(effective || !r.value("ok",false));
            emit("muted",{{"muted",effective || !r.value("ok",false)}});
            result(id,method,r);
          },false);
      });return;
    }
    if(method=="session.ping" || method=="session.refreshIce") {
      request(method,data,[this,id,method](Json r){
        if(method=="session.refreshIce" && r.value("ok",false)){ice_=r["data"].value("ice_servers",Json::array()); r["data"]=Json::object();}
        result(id,method,r);
      });return;
    }
    result(id,method,failure("",501,"功能尚未实现"));
  }
  Json config_,ice_=Json::array(),join_intent_=Json::object(),recv_consumers_=Json::object();
  MuteGate mute_gate_=std::make_shared<std::atomic<bool>>(true);
  std::shared_ptr<std::atomic<bool>> media_valid_=std::make_shared<std::atomic<bool>>(true);
  std::chrono::steady_clock::time_point resume_deadline_{};
  talk_event_fn callback_; void* user_;
  asio::io_context io_; asio::executor_work_guard<asio::io_context::executor_type> guard_;
  asio::steady_timer heartbeat_,reconnect_; Ws ws_; Ws::connection_ptr connection_;
  asio::thread_pool media_pool_; std::unique_ptr<Media> media_;
  std::thread thread_; std::atomic<uint64_t> next_operation_{1}; std::atomic<int> queued_{0};
  std::map<std::string,Pending> pending_;
  std::string url_,token_,session_,room_;
  uint64_t epoch_=0,media_epoch_=0,mute_epoch_=0,next_request_=0;
  bool stopping_=false,open_=false,ready_=false,retry_=false,joining_=false,disconnecting_=false;
  int retry_count_=0,heartbeat_seconds_=15,media_queued_=0,resume_seconds_=60;
  std::mt19937 random_{std::random_device{}()};
};
}
struct talk_handle { std::unique_ptr<talk::Core> core; };
extern "C" {
const char* talk_version(){return "0.1.0";}
talk_handle* talk_create(const talk_options* o) {
  if(!o || o->struct_size<sizeof(talk_options) || o->abi_version!=TALK_ABI_VERSION)return nullptr;
  try {
    auto config=talk::Json::parse(o->config_json ? o->config_json : "{}");
    if(!config.is_object())return nullptr;
    const auto backend=config.value("audio_backend","none");
    if(backend!="none"&&backend!="fake"&&backend!="wasapi"&&backend!="default")return nullptr;
    auto h=std::make_unique<talk_handle>();h->core=std::make_unique<talk::Core>(*o,config);return h.release();
  }catch(...){return nullptr;}
}
uint64_t talk_command(talk_handle* h,const char* method,const char* text) {
  if(!h||!method||!*method||!text)return 0;
  try { std::string s(text);if(s.size()>talk::kMaxMessage)return 0; auto data=talk::Json::parse(s);if(!data.is_object())return 0;return h->core->submit(method,std::move(data)); }
  catch(...){return 0;}
}
void talk_destroy(talk_handle* h){delete h;}
}
