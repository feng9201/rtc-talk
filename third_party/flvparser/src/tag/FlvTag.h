//
// Created by 16037 on 2023-07-04.
//

#pragma once
#include <string>
#include <cstdint>
#include <memory>
#include "tagData/FlvBaseTagData.h"
#include "utils/CircularBuffer.h"
namespace FlvParser {

    class FlvTag {
    private:
        uint32_t                        _previousTagSize;//4byte;
        uint8_t                         _type;//1byte;
        uint32_t                        _dataSize;//3byte;
        //uint32_t                        _timestamp;//3byte;
        //uint8_t                         _timestampExtended;//1byte;
        uint32_t                        _timestamp;//4byte;
        uint32_t                        _streamID;//3byte;
        std::unique_ptr<FlvBaseTagData> _data;//meybe zero
    public:
        FlvTag(CircularBuffer& buffer);
        ~FlvTag();

        bool isVaild() const;

        std::unique_ptr<Packet> packet();
    };

} //namespace FlvParser
