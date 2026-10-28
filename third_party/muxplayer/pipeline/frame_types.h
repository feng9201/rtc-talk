#pragma once

extern "C" {
#include <libavutil/frame.h>
}

namespace muxplay {
enum class FrameMemoryType {
    D3D11_Texture = 0,
    CPU_Buffer = 1,
};

struct DecodedFrame {
    AVFrame* avframe = nullptr;
    FrameMemoryType memoryType = FrameMemoryType::CPU_Buffer;
};
}  // namespace muxplay
