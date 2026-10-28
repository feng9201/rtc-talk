//
// Created by 16037 on 2023-07-04.
//

#include "FlvBaseTagData.h"
#include "info/FlvParserInfo.h"
#include "video/FlvVideoTagData.h"
#include "audio/FlvAudioTagData.h"
#include "application/FlvApplicationTagData.h"
//#include "script/FlvScriptTagData.h"
#include <iostream>

using namespace FlvParser;

FlvBaseTagData::~FlvBaseTagData() {

}

std::unique_ptr<Packet> FlvBaseTagData::packet() {
    return std::move(_packet);
}

std::unique_ptr<FlvBaseTagData> FlvBaseTagDataFactory::create(int64_t pts,const uint8_t& type,const uint8_t *data, const uint32_t& size) {
    switch (type){
        case FLV_TAG_TYPE::FLV_TAG_TYPE_AUDIO:
            return std::unique_ptr<FlvBaseTagData>(new FlvAudioTagData(pts,data,size));
        case FLV_TAG_TYPE::FLV_TAG_TYPE_VIDEO:
            return std::unique_ptr<FlvBaseTagData>(new FlvVideoTagData(pts,data,size));
//        case FLV_TAG_TYPE::FLV_TAG_TYPE_SCRIPT:
//            return std::shared_ptr<FlvBaseTagData>(new FlvScriptTagData(data,size));
        case FLV_TAG_TYPE::FLV_TAG_TYPE_APPLICATION:
            return std::unique_ptr<FlvBaseTagData>(new FlvApplicationTagData(pts, data, size));
        default:
        {
            std::cout<<"default................................................"<<std::endl;
            return std::unique_ptr<FlvBaseTagData>();
        }
    }
}
