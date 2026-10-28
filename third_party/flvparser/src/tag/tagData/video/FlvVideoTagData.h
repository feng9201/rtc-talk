//
// Created by 16037 on 2023-07-04.
//

#pragma once

#include "../FlvBaseTagData.h"
namespace FlvParser{

    class FlvVideoTagData final : public FlvBaseTagData {
    public:
        FlvVideoTagData(const int64_t pts,const uint8_t* data,const uint32_t size);
        ~FlvVideoTagData() override;
    };

}   // namespace FlvParser
