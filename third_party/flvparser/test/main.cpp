//
// Created by 16037 on 2023-07-04.
//
#include <iostream>
#include "FlvParser.h"
#include "info/FlvParserInfo.h"
#include <httplib.h>
#include <ctime>
#include <random>
#include <thread>
#include <future>
#include <atomic>
int main() {

    std::promise<bool> p;
    auto f = p.get_future();
    std::atomic<bool> running(true);
    std::shared_ptr<FLVParser> parser(new FLVParser());

    std::thread t([&](std::promise<bool> &&ret){
        auto self_parser = parser;
        bool first = true;
        httplib::Client cli("http://192.168.44.232:3310");
        httplib::Headers headers;
        auto res = cli.Get("/live_stream/2/stream.flv?stream_type=stream_main",
                           headers,
                           [&](const httplib::Response& response) {
                               auto ret = response.status == 200;
                               if (!ret) {
                                   std::cerr << "err status:" << response.status << std::endl;
                               }
                               return ret;
                           },
                           [&](const char* data, size_t data_length) {
                               if(self_parser->parse((const uint8_t*)data, data_length)){
                                   if(first){
                                       first = false;
                                       std::cout << "parse success" << std::endl;
                                       ret.set_value(true);
                                   }
                               }
                               return running.load();
                           });
		if (first) 
			ret.set_value(false);
    },std::move(p));

    std::thread t2;

    if(f.wait_for(std::chrono::milliseconds(1500)) != std::future_status::ready){
        std::cerr << "open flv stream failed" << std::endl;
        running.store(false);
        t.join();
    }else{
        t2 = std::thread([&] {
            while (running.load()) {
                auto pkt = parser->getPacket();
                if (pkt) {
                    std::cout << "pts:" << pkt->pts << " size:" << pkt->size << std::endl;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            }
        });
    }

    getchar();
    running.store(false);
    if(t.joinable())
        t.join();
    if(t2.joinable())
        t2.join();
    parser.reset();
	getchar();
}
