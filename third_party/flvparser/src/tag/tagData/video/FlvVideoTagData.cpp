//
// Created by 16037 on 2023-07-04.
//
#include <iostream>
#include <cstring>
#include "FlvVideoTagData.h"
#include "info/FlvParserInfo.h"
#include "utils/FlvUtils.h"
using namespace FlvParser;

FlvVideoTagData::FlvVideoTagData(const int64_t pts,const uint8_t *data, const uint32_t size) {
    std::unique_ptr<Packet> pkt;
    pkt.reset(new Packet);
    if(size > 4){
        char *p = (char*)data;
        pkt->pts = pts;
        pkt->index = FLV_TAG_TYPE::FLV_TAG_TYPE_VIDEO;
        pkt->codec_id = data[0] & 0x0f;
        pkt->key_frame = (data[0] & 0xf0) == 0b00010000;
        pkt->type = FlvUtils::valueFromBtye<uint8_t>(p+=1);
        //auto compositionTime = FlvUtils::valueFromBtye<int32_t>(p+=1,3);
        p+=4; // ->type->compositionTime
        pkt->size = size - (p - (char*)data);
        pkt->data = new uint8_t[pkt->size];
        if(!pkt->data){
            std::cerr << "new packet data failed" << std::endl;
            return;
        }
        memcpy(pkt->data,p,pkt->size);
        // ── 保持 AVCC 格式，不做 Annex-B 转换 ──
        // FLV seq header (type=0) 是 AVCDecoderConfigurationRecord，FLV 视频 NALU 包 (type=1)
        // 是 4 字节长度前缀格式。FFmpeg 根据 extradata (seq header) 自动设 is_avc=1，
        // 期望 packet 也是 AVCC 格式。两者保持原生 AVCC 即可正确匹配。
        if(pkt->codec_id != CODEC_ID::CODEC_ID_AVC
           && pkt->codec_id != CODEC_ID::CODEC_ID_HEVC){
            std::cerr << "unknown codec id: " << pkt->codec_id << std::endl;
        }

        _packet = std::move(pkt);
    }
}

FlvVideoTagData::~FlvVideoTagData() {
}

