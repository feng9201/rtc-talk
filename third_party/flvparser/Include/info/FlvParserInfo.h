//
// Created by 16037 on 2023-07-04.
//
#pragma

namespace FlvParser{

    enum FLV_TAG_TYPE{
        FLV_TAG_TYPE_AUDIO = 8,
        FLV_TAG_TYPE_VIDEO = 9,
        FLV_TAG_TYPE_SCRIPT = 18,
        FLV_TAG_TYPE_APPLICATION = 19
    };

    enum CODEC_ID{
        CODEC_ID_UNKNOWN = 0,
        CODEC_ID_AAC = 10,
        CODEC_ID_AVC = 7,
        CODEC_ID_HEVC = 12
    };

//    /// 视频流下标
//    using FLV_VIDEO_INDEX = FlvParser::FLV_TAG_TYPE::FLV_TAG_TYPE_VIDEO;
//    /// 音频流下标
//    using FLV_AUDIO_INDEX = FlvParser::FLV_TAG_TYPE::FLV_TAG_TYPE_AUDIO;

}
