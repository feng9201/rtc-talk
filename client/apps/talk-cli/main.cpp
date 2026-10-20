#include "talk/talk.h"
#include <nlohmann/json.hpp>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <fstream>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <sys/stat.h>
using Json=nlohmann::json;
struct App {
  talk_handle* core=nullptr;std::string room,invite,call_id,mode="sfu";bool audio=false,auto_accept=false;
  std::atomic<bool> joined{false},media_ready{false},failed{false},left{false},stats_pending{false},disconnected{false};
  std::mutex mutex;std::condition_variable condition;
  void command(const char* method,const Json& data){if(!talk_command(core,method,data.dump().c_str()))failed=true;}
  void enter_room(const std::string& room_id){
    mode="sfu";media_ready=false;
    if(!audio)media_ready=true;
    command("room.join",{{"room_id",room_id},{"display",room_id}});
  }
  static void event(void* context,const char* name,const char* text){
    auto& a=*static_cast<App*>(context);
    try {
      auto j=Json::parse(text);const std::string type=name;
      std::cout<<Json({{"event",type},{"data",j}}).dump()<<std::endl;
      if(type=="ready"&&!j.value("recovering",false)){
        if(!a.invite.empty())a.command("call.invite",{{"callees",Json::array({a.invite})},{"room_id",a.room}});
        // 呼叫等待方不先入房，否则 callee 会被服务端判定为忙。
        if(!a.auto_accept)a.enter_room(a.room);
      }
      if(type=="joined"){
        a.mode=j.value("media_mode","sfu");a.joined=true;
        if(!a.audio)a.command("media.setMute",{{"muted",false}});
      }
      if(type=="published"){
        if(a.audio)a.media_ready=true;
        a.command("media.setMute",{{"muted",false}});
      }
      if(type=="mediaMode")a.mode=j.value("media_mode","sfu");
      if(type=="p2p.state"&&j.value("state","")=="connected"){
        a.media_ready=true;
        a.command("media.setMute",{{"muted",false}});
      }
      if(type=="call.incoming"&&a.auto_accept){
        a.call_id=j.value("call_id","");
        a.command("call.ringing",{{"call_id",a.call_id}});
        a.command("call.accept",{{"call_id",a.call_id}});
        if(!a.joined)a.enter_room(j.value("room_id",a.room));
      }
      if(type=="call.state"){
        const auto state=j.value("state","");
        if(state=="accepted")a.call_id=j.value("call_id",a.call_id);
        if(state=="rejected"||state=="cancelled"||state=="timeout"||state=="ended"){
          a.call_id.clear();
          if(!a.joined)a.left=true;
        }
      }
      if(type=="left"||type=="room.closed"){a.left=true;a.joined=false;a.call_id.clear();}
      if(type=="result"&&j.value("method","")=="call.invite"&&j.value("ok",false))
        a.call_id=j["data"].value("call_id",a.call_id);
      if(type=="result"&&j.value("method","")=="media.stats")a.stats_pending=false;
      if(type=="result"&&j.value("method","")=="disconnect")a.disconnected=true;
      if(type=="media.error"||type=="error"||(type=="result"&&!j.value("ok",false)))a.failed=true;
      a.condition.notify_all();
    }catch(...){a.failed=true;a.condition.notify_all();}
  }
};
int main(int argc,char** argv){
  std::string url,token_file,config_file;bool token_stdin=false;int duration=10;App app;
  Json config={{"audio_backend","none"}};
  try {
    for(int i=1;i<argc;++i){
      const std::string arg=argv[i];
      if(arg=="--version"){std::cout<<talk_version()<<'\n';return 0;}
      if(arg=="--token-stdin"){token_stdin=true;continue;}
      if(arg=="--auto-accept"){app.auto_accept=true;continue;}
      if(i+1==argc)throw std::runtime_error("参数缺少值");
      const std::string value=argv[++i];
      if(arg=="--url")url=value;
      else if(arg=="--room")app.room=value;
      else if(arg=="--invite")app.invite=value;
      else if(arg=="--token-file")token_file=value;
      else if(arg=="--audio")config["audio_backend"]=value;
      else if(arg=="--media-mode")config["media_mode"]=value;
      else if(arg=="--config")config_file=value;
      else if(arg=="--duration")duration=std::stoi(value);
      else throw std::runtime_error("未知参数");
    }
    if(!config_file.empty()){std::ifstream in(config_file);in>>config;}
    const auto media_mode=config.value("media_mode","auto");
    if(media_mode!="auto"&&media_mode!="sfu"&&media_mode!="p2p")throw std::runtime_error("media_mode 只能是 auto/sfu/p2p");
    if(url.empty()||app.room.empty()||duration<1||duration>86400||token_stdin==!token_file.empty())
      throw std::runtime_error("用法：talk-cli --url ws://主机:端口/talk/v1/ws --room 房间 --token-file 文件（或 --token-stdin） [--audio none|fake|alsa|pulse] [--media-mode auto|sfu|p2p] [--invite 被叫] [--auto-accept] [--config 文件] [--duration 秒]");
    std::string token;
    if(token_stdin)std::getline(std::cin,token);
    else{
      struct stat s{};if(stat(token_file.c_str(),&s)!=0||(s.st_mode&0077)!=0)throw std::runtime_error("token 文件权限必须限制为当前用户");
      std::ifstream in(token_file);std::getline(in,token);
    }
    if(token.empty())throw std::runtime_error("token 为空");
    app.audio=config.value("audio_backend","none")!="none";
    const auto cfg=config.dump();talk_options options{sizeof(talk_options),TALK_ABI_VERSION,App::event,&app,cfg.c_str()};
    app.core=talk_create(&options);if(!app.core)throw std::runtime_error("创建 core 失败");
    app.command("connect",{{"url",url},{"token",token}});token.clear();
    {
      std::unique_lock<std::mutex> lock(app.mutex);
      app.condition.wait_for(lock,std::chrono::seconds(25),[&]{return app.failed||(app.joined&&(!app.audio||app.media_ready));});
    }
    if(!app.joined||(app.audio&&!app.media_ready))app.failed=true;
    for(int i=0;i<duration&&!app.failed;++i){
      std::this_thread::sleep_for(std::chrono::seconds(1));
      if(app.audio&&(i%5==0||i==duration-1)&&!app.stats_pending){
        app.stats_pending=true;app.command("media.stats",Json::object());
      }
    }
    if(app.stats_pending){
      std::unique_lock<std::mutex> lock(app.mutex);
      if(!app.condition.wait_for(lock,std::chrono::seconds(10),[&]{return !app.stats_pending;}))app.failed=true;
    }
    if(!app.call_id.empty())app.command("call.hangup",{{"call_id",app.call_id}});
    else if(app.joined)app.command("room.leave",Json::object());
    if(app.joined||!app.call_id.empty()){
      std::unique_lock<std::mutex> lock(app.mutex);app.condition.wait_for(lock,std::chrono::seconds(3),[&]{return app.left.load();});
    }
    app.command("disconnect",Json::object());
    {
      std::unique_lock<std::mutex> lock(app.mutex);
      if(!app.condition.wait_for(lock,std::chrono::seconds(11),[&]{return app.disconnected.load();}))app.failed=true;
    }
    talk_destroy(app.core);app.core=nullptr;return app.failed?1:0;
  }catch(const std::exception& e){std::cerr<<e.what()<<'\n';if(app.core)talk_destroy(app.core);return 2;}
}
