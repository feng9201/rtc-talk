//
// Created by 16037 on 2023-07-04.
//

#include <iostream>
#include <cstring>
#include "FlvAudioTagData.h"
#include "info/FlvParserInfo.h"
#include "utils/FlvUtils.h"
using namespace FlvParser;

FlvAudioTagData::FlvAudioTagData(const int64_t pts,const uint8_t *data, const uint32_t size) {
    std::unique_ptr<Packet> pkt;
    pkt.reset(new Packet);
    if(size > 2){
        char *p = (char*)data;
        pkt->pts = pts;
        pkt->index = FLV_TAG_TYPE::FLV_TAG_TYPE_AUDIO;
        pkt->codec_id = (data[0] & 0xf0) >> 4;
        pkt->type = (data[1] & 0x0f);
        pkt->size = size - 2;
        pkt->data = new uint8_t[pkt->size];
        if(!pkt->data){
            std::cerr << "new packet data failed" << std::endl;
            return;
        }
        memcpy(pkt->data,p+2,pkt->size);
        if(pkt->codec_id != CODEC_ID::CODEC_ID_AAC)
            std::cerr << "unknown codec id: " << pkt->codec_id << std::endl;
        _packet = std::move(pkt);
    }
}

FlvAudioTagData::~FlvAudioTagData() {
    _packet.reset();
}

