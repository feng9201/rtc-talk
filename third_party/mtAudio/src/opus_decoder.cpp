#include "decoder_internal.h"
#include <cstring>
#include <cstdio>

namespace mtAudio {

// ═══════════════════════════════════════════════════════════
// Opus 解码器实现
// ═══════════════════════════════════════════════════════════

OpusDecoder::OpusDecoder()
{
}

OpusDecoder::~OpusDecoder()
{
    if (handle_) {
        opus_decoder_destroy(handle_);
        handle_ = nullptr;
    }
}

bool OpusDecoder::init(const uint8_t* extraData, int extraDataLen,
                       int sampleRate, int channels)
{
    if (sampleRate <= 0)   sampleRate = 48000;   // Opus 默认
    if (channels   <= 0)   channels   = 2;       // Opus 默认立体声

    int err = OPUS_OK;
    handle_ = opus_decoder_create(sampleRate, channels, &err);
    if (err != OPUS_OK || !handle_) {
        fprintf(stderr, "[OpusDecoder] create failed: %s\n", opus_strerror(err));
        return false;
    }

    outputSampleRate_ = sampleRate;
    outputChannels_   = channels;

#ifdef MT_AUDIO_ENABLE_RESAMPLE
    if (!resampler_.init(outputSampleRate_, outputChannels_, 16)) {
        fprintf(stderr, "[OpusDecoder] resampler init failed\n");
        return false;
    }
#endif

    inited_ = true;
    return true;
}

int OpusDecoder::decode(const uint8_t* data, int len,
                        AudioFrame& outFrame, int64_t pts)
{
    if (!handle_ || !inited_ || !data || len <= 0) return -1;

    // Opus 最大帧长：120ms @ 48kHz = 5760 样本/声道
    constexpr int kMaxFrameSamples = 5760;
    std::vector<opus_int16> pcmBuf(kMaxFrameSamples * outputChannels_);

    int numSamples = opus_decode(handle_,
                                  data, len,
                                  pcmBuf.data(),
                                  kMaxFrameSamples,
                                  /* decode_fec */ 0);

    if (numSamples < 0) {
        fprintf(stderr, "[OpusDecoder] decode err: %s\n", opus_strerror(numSamples));
        return -1;
    }
    if (numSamples == 0) return 0;  // 空帧（DTX/静音）

    int dataSize = numSamples * outputChannels_ * sizeof(opus_int16);
    const uint8_t* pcmData = reinterpret_cast<const uint8_t*>(pcmBuf.data());

#ifdef MT_AUDIO_ENABLE_RESAMPLE
    std::vector<uint8_t> resampled;
    if (!resampler_.resample(pcmData, dataSize, resampled)) return -1;

    outFrame.data.assign(resampled.begin(), resampled.end());
    outFrame.sampleRate    = resampler_.needsResample() ? 16000 : outputSampleRate_;
    outFrame.channels      = resampler_.needsResample() ? 1 : outputChannels_;
    outFrame.bitsPerSample = 16;
    outFrame.numSamples    = resampler_.needsResample()
        ? static_cast<int>(outFrame.data.size() / (outFrame.channels * sizeof(opus_int16)))
        : numSamples;
#else
    outFrame.data.assign(pcmData, pcmData + dataSize);
    outFrame.sampleRate    = outputSampleRate_;
    outFrame.channels      = outputChannels_;
    outFrame.bitsPerSample = 16;
    outFrame.numSamples    = numSamples;
#endif

    outFrame.pts = pts;
    return numSamples;
}

void OpusDecoder::reset()
{
    if (handle_) {
        opus_decoder_destroy(handle_);
        handle_ = nullptr;
    }
    int err = OPUS_OK;
    handle_ = opus_decoder_create(outputSampleRate_, outputChannels_, &err);
    inited_     = false;
#ifdef MT_AUDIO_ENABLE_RESAMPLE
    resampler_.reset();
#endif
}

}  // namespace mtAudio
