#pragma once

#include "../pipeline/idecoder.h"
#include "../multiplex_player_private.h"
#include "ffmpeg_decoder.h"
#include <memory>

extern "C" {
#include <libavutil/hwcontext.h>
#include <libavutil/pixfmt.h>
}

namespace muxplay {

// ── 硬件解码器（D3D11VA）──
// 实现 IDecoder 接口，封装 D3D11VA 硬件加速初始化和解码逻辑。
// 内部持有 hw_device_ctx_ 和 FFmpegDecoder。
class HardwareDecoder : public IDecoder {
public:
    explicit HardwareDecoder(MultiplexPlayerPrivate* priv);
    ~HardwareDecoder() override;

    // 初始化硬件解码器（D3D11VA 设备创建 + avcodec_open2）
    // 调用前需确保 priv->formatContext_ / videoStreamIndex_ 已有效
    bool init();

    // ── IDecoder 接口 ──
    DecodeResult decode(DecodedFrame& out) override;
    bool hasPendingPacket() const override;
    void close() override;

private:
    // D3D11VA 设备初始化（从 ffmpeg_demuxing.cpp 迁移）
    bool initHWDevice(const AVCodec* codec, ID3D11Device* sharedDevice);
    static AVPixelFormat getHWFormat(AVCodecContext* s, const enum AVPixelFormat* fmt);

    MultiplexPlayerPrivate* priv_ = nullptr;
    std::unique_ptr<FFmpegDecoder> ffmpegDecoder_;
    AVBufferRef* hw_device_ctx_ = nullptr;
    int  pix_format_ = -1;
    bool inited_ = false;
};

} // namespace muxplay
