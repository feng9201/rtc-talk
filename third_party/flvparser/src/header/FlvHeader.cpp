#include "FlvHeader.h"
#include "utils/FlvUtils.h"
#include <cstring>
#include <string>
#include <memory>
#include <iostream>


using namespace FlvParser;
const int MIN_HEADER_SIZE = 9 + 4; // flv header + previous tag size
const std::string FLVHEADER = "FLV";

const uint8_t VIDEO_FLAG = 0x1;             // u(1:b0)
const uint8_t AUDIO_FLAG = VIDEO_FLAG << 2; // u(1:b2)

FlvHeader::FlvHeader(CircularBuffer& buffer) {
    if (buffer.size() < MIN_HEADER_SIZE)
        return;
    char* p = buffer.data_ptr(MIN_HEADER_SIZE);

    memset(_fileType, 0, 4);
    memcpy(_fileType, p, 3);

    // version 1byte
    _version = FlvUtils::valueFromBtye<int8_t>(p += 3);

    // stream info 1byte
    _streamInfo = FlvUtils::valueFromBtye<int8_t>(++p);

    // header size 4byte
    _headerSize = FlvUtils::valueFromBtye<uint32_t>(++p);

    auto preTagSize = FlvUtils::valueFromBtye<uint32_t>(p += 4);

    if (_headerSize <= buffer.size() && preTagSize == 0)
        buffer.erase_front(_headerSize + 4);
    else
        _headerSize = 0;
}

FlvHeader::~FlvHeader() {
    std::cout << "~FlvHeader" << std::endl;
}

bool FlvHeader::isValid() const {
    return _headerSize != 0;
}

bool FlvHeader::hasAudio() const {
    return _streamInfo & AUDIO_FLAG;
}

bool FlvHeader::hasVideo() const {
    return _streamInfo & VIDEO_FLAG;
}

