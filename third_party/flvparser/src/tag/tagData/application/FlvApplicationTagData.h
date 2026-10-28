//
// Created by 16037 on 2023-07-04.
//

#pragma once

#include "../FlvBaseTagData.h"
namespace FlvParser {

    class FlvApplicationTagData final : public FlvBaseTagData {
        //std::shared_ptr<Packet> _packet;
    public:
        FlvApplicationTagData(const int64_t pts, const uint8_t* data, const uint32_t size);
        ~FlvApplicationTagData() override;
        //std::shared_ptr<Packet> packet() override;
    };

}   // namespace FlvParser
