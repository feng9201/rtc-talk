//
// Created by 16037 on 2023-07-05.
//

#pragma once
#include "../FlvBaseTagData.h"
namespace FlvParser {

    class FlvScriptTagData final : public FlvBaseTagData {
    public:
        FlvScriptTagData(const uint8_t* data,const uint32_t size);
        ~FlvScriptTagData() override;
    };

} // FlvParser
