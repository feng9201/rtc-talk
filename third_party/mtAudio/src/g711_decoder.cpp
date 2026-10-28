#include "decoder_internal.h"
#include "../g711/g711.h"
#include <cstring>
#include <cstdio>

namespace mtAudio {

G711Decoder::G711Decoder(AudioCodec codec)
    : codecType_(codec)
{
}

bool G711Decoder::init(const uint8_t* /*extraData*/, int /*extraDataLen*/,
                       int sampleRate, int channels)
{
    // G.711 参数固定：8000Hz, mono, 8-bit → 解码后 16-bit
    if (sampleRate <= 0) sampleRate = 8000;
    if (channels   <= 0) channels   = 1;

    outputSampleRate_ = sampleRate;
    outputChannels_   = channels;

#ifdef MT_AUDIO_ENABLE_RESAMPLE
    if (!resampler_.init(outputSampleRate_, outputChannels_, 16)) {
        fprintf(stderr, "[G711Decoder] resampler init failed\n");
        return false;
    }
#endif

    inited_ = true;
    return true;
}

int G711Decoder::decode(const uint8_t* data, int len,
                        AudioFrame& outFrame, int64_t pts)
{
    if (!inited_ || !data || len <= 0) return -1;

    // G.711: 每个字节 → 1 个 int16_t 样本
    std::vector<int16_t> pcmBuf(len);

    if (codecType_ == AudioCodec::G711A) {
        for (int i = 0; i < len; ++i)
            pcmBuf[i] = alaw2linear(data[i]);
    } else {
        for (int i = 0; i < len; ++i)
            pcmBuf[i] = ulaw2linear(data[i]);
    }

    int dataSize = len * sizeof(int16_t);
    const uint8_t* pcmData = reinterpret_cast<const uint8_t*>(pcmBuf.data());

#ifdef MT_AUDIO_ENABLE_RESAMPLE
    std::vector<uint8_t> resampled;
    if (!resampler_.resample(pcmData, dataSize, resampled)) return -1;

    outFrame.data.assign(resampled.begin(), resampled.end());
    outFrame.sampleRate    = resampler_.needsResample() ? 16000 : outputSampleRate_;
    outFrame.channels      = resampler_.needsResample() ? 1 : outputChannels_;
    outFrame.bitsPerSample = 16;
    outFrame.numSamples    = resampler_.needsResample()
        ? static_cast<int>(outFrame.data.size() / (outFrame.channels * sizeof(int16_t)))
        : len;
#else
    outFrame.data.assign(pcmData, pcmData + dataSize);
    outFrame.sampleRate    = outputSampleRate_;
    outFrame.channels      = outputChannels_;
    outFrame.bitsPerSample = 16;
    outFrame.numSamples    = len;
#endif

    outFrame.pts = pts;
    return len;
}

void G711Decoder::reset()
{
    inited_ = false;
#ifdef MT_AUDIO_ENABLE_RESAMPLE
    resampler_.reset();
#endif
}

}  // namespace mtAudio
