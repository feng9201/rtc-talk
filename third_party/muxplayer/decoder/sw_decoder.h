#pragma once

#include "../pipeline/idecoder.h"
#include "../multiplex_player_private.h"
#include "ffmpeg_decoder.h"
#include <memory>

namespace muxplay {

// ── 软件解码器 ──
// 实现 IDecoder 接口，使用 FFmpeg 原生软件解码（无硬件加速）。
// 输出 FrameMemoryType::CPU_Buffer 类型的 AVFrame（YUV 数据在 CPU 侧）。
class SoftwareDecoder : public IDecoder {
public:
    explicit SoftwareDecoder(MultiplexPlayerPrivate* priv);
    ~SoftwareDecoder() override;

    // 初始化软件解码器（avcodec_open2，无 HW 上下文）
    bool init();

    // ── IDecoder 接口 ──
    DecodeResult decode(DecodedFrame& out) override;
    bool hasPendingPacket() const override;
    void close() override;

private:
    MultiplexPlayerPrivate* priv_ = nullptr;
    std::unique_ptr<FFmpegDecoder> ffmpegDecoder_;
    bool inited_ = false;
};

} // namespace muxplay
