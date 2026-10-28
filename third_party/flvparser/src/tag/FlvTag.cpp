//
// Created by 16037 on 2023-07-04.
//

#include "FlvTag.h"
#include "utils/FlvUtils.h"
#include <cstring>
#include <string>
#include <memory>
#include <iostream>

using namespace FlvParser;
const int TAG_HEADER_SIZE = 11;
const int MIN_TAG_SIZE = TAG_HEADER_SIZE + 4; //tag header size(11) + previous tag size(4)

FlvTag::FlvTag(CircularBuffer& buffer) {
    if (buffer.size() < MIN_TAG_SIZE)
        return;
    char* p = buffer.data_ptr(MIN_TAG_SIZE);
    if (p == nullptr)
        return;
    _type = FlvUtils::valueFromBtye<uint8_t>(p);
    //std::cout<<"type: "<<(int)_type<<std::endl;
    _dataSize = FlvUtils::valueFromBtye<uint32_t>(p += 1, 3);
    _timestamp = FlvUtils::valueFromBtye<uint32_t>(p += 3, 3);
    //std::cout<<"_timestamp-------->"<<_timestamp<<"-------->_dataSize"<<_dataSize<<std::endl;
    auto timestampExtended = FlvUtils::valueFromBtye<uint8_t>(p += 3);
    _timestamp |= timestampExtended << 24;
    _streamID = FlvUtils::valueFromBtye<uint32_t>(p += 1, 3);
    if (buffer.size() < MIN_TAG_SIZE + _dataSize) {
        return;
    }
    else {
        p = buffer.data_ptr(MIN_TAG_SIZE + _dataSize);
        if (p == nullptr)
            return;
        p += 8;
        _data = std::move(FlvBaseTagDataFactory::create(_timestamp, _type, (uint8_t*)(p += 3), _dataSize));
        _previousTagSize = FlvUtils::valueFromBtye<uint32_t>(p += _dataSize);
        if (_previousTagSize != TAG_HEADER_SIZE + _dataSize)
            _previousTagSize = 0;
        else {
            buffer.erase_front(MIN_TAG_SIZE + _dataSize);
        }
    }
}

FlvTag::~FlvTag() {

}

std::unique_ptr<Packet> FlvTag::packet() {
    if (_data) {
        return std::move(_data->packet());
    }
    return nullptr;
}

bool FlvTag::isVaild() const {
    return _previousTagSize == (TAG_HEADER_SIZE + _dataSize);
}
