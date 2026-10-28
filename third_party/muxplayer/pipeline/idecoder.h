#pragma once

#include "frame_types.h"
#include "../decoder/ffmpeg_decoder.h"  // DecodeResult

namespace muxplay {

// ── 解码器抽象 ──
// 实现需支持 send/receive 模式（FFmpeg avcodec_send_packet / avcodec_receive_frame）。
// 调用者从 packet 队列取包投喂解码器，解码器产出 DecodedFrame 给下游 FrameConsumer。
class IDecoder {
public:
    virtual ~IDecoder() = default;

    // 解码一帧。返回 DecodeResult 指示解码状态：
    //   GotFrame     - 成功获取一帧（out 有效）
    //   NeedMoreData - 队列空，需等待更多 packet
    //   Delayed      - send 成功但 receive EAGAIN（高延迟编解码器正常行为）
    //   Error        - 真正的解码错误
    // out.avframe 的所有权转移给调用者，调用者负责 av_frame_free。
    virtual DecodeResult decode(DecodedFrame& out) = 0;

    // 是否有因 EAGAIN 暂存未成功发送的 packet，需要优先重发
    virtual bool hasPendingPacket() const = 0;

    // 释放解码器资源（avcodec_free_context 等）
    virtual void close() = 0;
};

} // namespace muxplay
