//
// Created by 16037 on 2023-07-04.
//

#pragma once
#include "packet/Packet.h"

namespace FlvParser{

    class FlvBaseTagData {
    protected:
        std::unique_ptr<Packet> _packet;
    public:
        virtual ~FlvBaseTagData();
        virtual std::unique_ptr<Packet> packet();
    };

    class FlvBaseTagDataFactory{
    public:
        static std::unique_ptr<FlvBaseTagData> create(const int64_t pts,const uint8_t& type,const uint8_t* data,const uint32_t& size);
    };

}   // namespace FlvParser
