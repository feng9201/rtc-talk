//
// Created by 16037 on 2023-07-04.
//

#pragma once
#include "../FlvBaseTagData.h"
namespace FlvParser{

    class FlvAudioTagData final : public FlvBaseTagData {
    public:
        FlvAudioTagData(const int64_t pts,const uint8_t* data,const uint32_t size);
        ~FlvAudioTagData() override;
    };

}   // namespace FlvParser
