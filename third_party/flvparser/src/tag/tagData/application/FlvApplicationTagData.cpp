//
// Created by 16037 on 2023-07-04.
//
#include <iostream>
#include <cstring>
#include "FlvApplicationTagData.h"
#include "info/FlvParserInfo.h"
#include "utils/FlvUtils.h"
using namespace FlvParser;

FlvApplicationTagData::FlvApplicationTagData(const int64_t pts, const uint8_t* data, const uint32_t size) {
    std::unique_ptr<Packet> pkt;
    pkt.reset(new Packet);
    if (size == 8) {
        char* p = (char*)data;
        pkt->pts = pts;
        pkt->index = FLV_TAG_TYPE::FLV_TAG_TYPE_APPLICATION;
        pkt->track_id = FlvUtils::valueFromBtye<uint64_t>(p);
        pkt->type = 0;
        pkt->has_track_id = true;
        //std::cout << "track_id: " << pkt->track_id << "| hex:";
        //for (int i = 0; i < 8; i++) {
        //    std::cout << std::hex << (int)p[i] << " ";
        //}
        //std::cout << std::endl;

        _packet = std::move(pkt);
    }
    else
        std::cerr << "unknown application tag size: " << size << std::endl;
}

FlvApplicationTagData::~FlvApplicationTagData() {
    _packet.reset();
}

// std::unique_ptr<Packet> FlvApplicationTagData::packet() {
//     return _packet;
// }
