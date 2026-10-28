#pragma once

#include <cstdint>
#include <string>
#include "utils/CircularBuffer.h"


namespace FlvParser {

    class FlvHeader {
    private:
        char                _fileType[4];//3byte;
        int8_t              _version;//1byte;
        int8_t              _streamInfo;//1byte;
        uint32_t            _headerSize;//4byte;

    public:
        FlvHeader(CircularBuffer& buffer);
        FlvHeader(const FlvHeader& header) = delete;
        FlvHeader& operator=(const FlvHeader& header) = delete;
        ~FlvHeader();

        /// 是否有效
        /// \return true 有效 false 无效
        bool isValid() const;

        /// 是否有音频
        /// \return true 有音频 false 无音频
        bool hasAudio() const;

        /// 是否有视频
        /// \return true 有视频 false 无视频
        bool hasVideo() const;
    };

} //namespace FlvParser
