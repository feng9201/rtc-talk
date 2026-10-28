#include "sw_decoder.h"

namespace muxplay {

SoftwareDecoder::SoftwareDecoder(MultiplexPlayerPrivate* priv)
    : priv_(priv)
{
}

SoftwareDecoder::~SoftwareDecoder()
{
    close();
}

bool SoftwareDecoder::init()
{
    if (!priv_) return false;

#ifdef MUXPLAY_ENABLE_FLV
    // ── FLV 拉流模式：codecContext_ 已由 openFlv() 创建并配置好 extradata ──
    if (priv_->isFlvStream_) {
        if (!priv_->codecContext_) return false;

        priv_->video_width_  = priv_->codecContext_->width;
        priv_->video_height_ = priv_->codecContext_->height;

        // 打开解码器（纯软解，无 hw_device_ctx / get_format 回调）
        if (avcodec_open2(priv_->codecContext_, priv_->codecContext_->codec, nullptr) < 0) {
            return false;
        }

        ffmpegDecoder_ = std::make_unique<FFmpegDecoder>(priv_);
        inited_ = true;
        return true;
    }
#endif

    if (!priv_->formatContext_) return false;
    if (priv_->videoStreamIndex_ < 0) return false;

    AVCodecParameters* codecpar =
        priv_->formatContext_->streams[priv_->videoStreamIndex_]->codecpar;
    const AVCodec* codec = avcodec_find_decoder(codecpar->codec_id);
    if (!codec) return false;

    // 创建解码器上下文（无 HW 设备）
    priv_->codecContext_ = avcodec_alloc_context3(codec);
    if (avcodec_parameters_to_context(priv_->codecContext_, codecpar) != 0) {
        return false;
    }

    // 读取分辨率
    int width  = priv_->codecContext_->width;
    int height = priv_->codecContext_->height;
    if (width <= 0 || height <= 0) {
        width  = codecpar->width;
        height = codecpar->height;
    }
    priv_->video_width_  = width;
    priv_->video_height_ = height;

    // 打开解码器（纯软解，无 hw_device_ctx / get_format 回调）
    if (avcodec_open2(priv_->codecContext_, codec, nullptr) < 0) {
        return false;
    }

    ffmpegDecoder_ = std::make_unique<FFmpegDecoder>(priv_);
    inited_ = true;
    return true;
}

DecodeResult SoftwareDecoder::decode(DecodedFrame& out)
{
    if (!inited_ || !ffmpegDecoder_) return DecodeResult::Error;

    out.avframe = av_frame_alloc();
    DecodeResult result = ffmpegDecoder_->VideoDecoder(out.avframe);

    if (result == DecodeResult::GotFrame) {
        // 软解输出始终是 CPU 侧 YUV 数据
        out.memoryType = FrameMemoryType::CPU_Buffer;
    } else {
        av_frame_free(&out.avframe);
        out.avframe = nullptr;
    }
    return result;
}

bool SoftwareDecoder::hasPendingPacket() const
{
    return ffmpegDecoder_ ? ffmpegDecoder_->hasPendingPacket() : false;
}

void SoftwareDecoder::close()
{
    if (ffmpegDecoder_) {
        ffmpegDecoder_->close();
        ffmpegDecoder_.reset();
    }
    inited_ = false;
}

} // namespace muxplay
