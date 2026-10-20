#include "signaling/protocol.h"
#include <iostream>
#include <vector>
int main(){
  using namespace talk;
  int tests=0;
  const std::vector<std::string> valid={
    R"({"v":1,"type":"req","id":"s-1","method":"sfu.offer","extra":true})",
    R"({"v":1,"type":"res","id":"c-1","ok":true,"data":{}})",
    R"({"v":1,"type":"res","id":"c-1","ok":false,"error":{"code":403}})",
    R"({"v":1,"type":"ntf","method":"room.peerLeft","data":{"uid":"甲"}})"
  };
  for(const auto& text:valid){try{parse_message(text);++tests;}catch(...){std::cerr<<"有效报文被拒绝\n";return 1;}}
  auto invalid=std::vector<std::string>{"{","[]","null","{}",
    R"({"v":2,"type":"req","id":"c-1","method":"x"})",
    R"({"v":1,"type":"req","id":1,"method":"x"})",
    R"({"v":1,"type":"res","id":"c-1","ok":"true"})",
    R"({"v":1,"type":"res","id":"c-1","ok":false})",
    R"({"v":1,"type":"req","id":"","method":"x"})",
    R"({"v":1,"type":"ntf","method":false})",
    R"({"v":1,"type":"bogus"})",std::string(kMaxMessage+1,'x')};
  for(const auto& text:invalid){bool threw=false;try{parse_message(text);}catch(...){threw=true;}if(!threw){std::cerr<<"无效报文被接受\n";return 1;}++tests;}
  if(parse_message(response("s-1",{{"sdp","answer"}}).dump())["id"]!="s-1")return 1;
  if(parse_message(failure("s-2",501,"未实现").dump())["error"]["code"]!=501)return 1;
  std::cout<<tests+2<<" 个协议检查通过\n";return 0;
}
