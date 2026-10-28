#include "decoder_internal.h"
#include <cstring>
#include <cstdio>

namespace mtAudio {

// ═══════════════════════════════════════════════════════════
// AAC 解码器实现
// ═══════════════════════════════════════════════════════════

namespace {
// 标准采样率表（ISO 14496-3）
static const int kSampleRates[] = {
    96000, 88200, 64000, 48000, 44100, 32000,
    24000, 22050, 16000, 12000, 11025, 8000, 7350
};

// 从 AudioSpecificConfig 解析 sampleRate 和 channels
// 返回 true 表示解析成功
bool parseAudioSpecificConfig(const uint8_t* data, int len,
                               int& outSampleRate, int& outChannels)
{
    if (!data || len < 2) return false;

    // 将字节流作为 16-bit big-endian 读取
    uint16_t bits = ((uint16_t)data[0] << 8) | data[1];

    // audioObjectType: bits[15:11], 5 bits
    int audioObjectType = (bits >> 11) & 0x1F;
    if (audioObjectType == 31) {
        // extended: need 6 more bits (not handled in 2-byte config)
        return false;
    }

    // samplingFrequencyIndex: bits[10:7], 4 bits
    int freqIdx = (bits >> 7) & 0x0F;
    if (freqIdx == 0x0F) {
        // 24-bit explicit frequency (need 4 bytes)
        return false;
    }
    if (freqIdx >= 13) return false;
    outSampleRate = kSampleRates[freqIdx];

    // channelConfiguration: bits[6:3], 4 bits
    int chanConfig = (bits >> 3) & 0x0F;
    if (chanConfig == 0) return false;  // program-specific
    if (chanConfig > 7) return false;   // invalid
    outChannels = chanConfig;

    return true;
}
} // anon namespace

AacDecoder::AacDecoder()
{
    handle_ = aacDecoder_Open(TT_MP4_RAW, /* nrOfLayers */ 1);
}

AacDecoder::~AacDecoder()
{
    if (handle_) aacDecoder_Close(handle_);
}

bool AacDecoder::init(const uint8_t* extraData, int extraDataLen,
                      int sampleRate, int channels)
{
    if (!handle_ || !extraData || extraDataLen <= 0) return false;

    // 传递 AudioSpecificConfig
    UCHAR* conf[] = { const_cast<UCHAR*>(extraData) };
    UINT   len[]  = { static_cast<UINT>(extraDataLen) };
    AAC_DECODER_ERROR err = aacDecoder_ConfigRaw(handle_, conf, len);
    if (err != AAC_DEC_OK) {
        fprintf(stderr, "[AacDecoder] ConfigRaw failed: 0x%x\n", err);
        return false;
    }

    // 优先从 fdk-aac 获取 stream info
    CStreamInfo* info = aacDecoder_GetStreamInfo(handle_);
    if (info && info->sampleRate > 0) {
        outputSampleRate_ = info->sampleRate;
        outputChannels_   = info->numChannels;
        fprintf(stderr, "[AacDecoder] info from GetStreamInfo: %dHz %dch\n",
                outputSampleRate_, outputChannels_);
    } else {
        // fdk-aac TT_MP4_RAW 模式有时不返回 info，自己解析 AudioSpecificConfig
        int parsedSr = 0, parsedCh = 0;
        if (parseAudioSpecificConfig(extraData, extraDataLen, parsedSr, parsedCh)) {
            outputSampleRate_ = parsedSr;
            outputChannels_   = parsedCh;
            fprintf(stderr, "[AacDecoder] info from AudioSpecificConfig: %dHz %dch\n",
                    outputSampleRate_, outputChannels_);
        } else {
            // 最后使用传入的参数
            if (sampleRate > 0 && channels > 0) {
                outputSampleRate_ = sampleRate;
                outputChannels_   = channels;
                fprintf(stderr, "[AacDecoder] info from caller params: %dHz %dch\n",
                        outputSampleRate_, outputChannels_);
            } else {
                fprintf(stderr, "[AacDecoder] cannot determine sampleRate/channels!\n");
                return false;
            }
        }
    }

    infoReady_ = true;

    // 初始化重采样器（MT_AUDIO_ENABLE_RESAMPLE 宏控制）
#ifdef MT_AUDIO_ENABLE_RESAMPLE
    if (!resampler_.init(outputSampleRate_, outputChannels_, 16)) {
        fprintf(stderr, "[AacDecoder] resampler init failed\n");
        return false;
    }
#endif

    inited_ = true;
    return true;
}

int AacDecoder::decode(const uint8_t* data, int len,
                       AudioFrame& outFrame, int64_t pts)
{
    if (!handle_ || !inited_ || !data || len <= 0) return -1;

    // 填充数据
    UCHAR* buf[]     = { const_cast<UCHAR*>(data) };
    UINT   bufSize[] = { static_cast<UINT>(len) };
    UINT   bytesValid = static_cast<UINT>(len);

    AAC_DECODER_ERROR err = aacDecoder_Fill(handle_, buf, bufSize, &bytesValid);
    if (err != AAC_DEC_OK) {
        fprintf(stderr, "[AacDecoder] Fill failed: 0x%x\n", err);
        return -1;
    }

    // 解码
    INT_PCM pcmBuf[8192];  // 足够容纳 AAC 一帧（1024 samples × 8 ch max）
    err = aacDecoder_DecodeFrame(handle_, pcmBuf,
                                  sizeof(pcmBuf) / sizeof(INT_PCM), 0);

    if (err == AAC_DEC_NOT_ENOUGH_BITS) return 0;  // 还没凑够一帧
    if (err != AAC_DEC_OK) {
        fprintf(stderr, "[AacDecoder] DecodeFrame err: 0x%x\n", err);
        return -1;
    }

    CStreamInfo* info = aacDecoder_GetStreamInfo(handle_);
    if (!info || info->frameSize <= 0) return -1;

    int numSamples = info->frameSize;
    int channels   = info->numChannels;
    int dataSize   = numSamples * channels * sizeof(short);

    const uint8_t* pcmData = reinterpret_cast<const uint8_t*>(pcmBuf);

#ifdef MT_AUDIO_ENABLE_RESAMPLE
    // 重采样
    std::vector<uint8_t> resampled;
    if (!resampler_.resample(pcmData, dataSize, resampled)) return -1;

    outFrame.data.assign(resampled.begin(), resampled.end());
    outFrame.sampleRate    = resampler_.needsResample() ? 16000 : outputSampleRate_;
    outFrame.channels      = resampler_.needsResample() ? 1 : channels;
    outFrame.bitsPerSample = 16;
    outFrame.numSamples    = numSamples * (resampler_.needsResample()
                              ? (16000 * 1) / (outputSampleRate_ * channels)
                              : 1);
    if (resampler_.needsResample()) {
        outFrame.numSamples = static_cast<int>(outFrame.data.size()
                              / (outFrame.channels * outFrame.bitsPerSample / 8));
    } else {
        outFrame.numSamples = numSamples;
    }
#else
    outFrame.data.assign(pcmData, pcmData + dataSize);
    outFrame.sampleRate    = outputSampleRate_;
    outFrame.channels      = channels;
    outFrame.bitsPerSample = 16;
    outFrame.numSamples    = numSamples;
#endif

    outFrame.pts = pts;
    return numSamples;
}

void AacDecoder::reset()
{
    if (handle_) {
        aacDecoder_Close(handle_);
        handle_ = aacDecoder_Open(TT_MP4_RAW, 1);
    }
    inited_     = false;
    infoReady_  = false;
#ifdef MT_AUDIO_ENABLE_RESAMPLE
    resampler_.reset();
#endif
}

}  // namespace mtAudio
