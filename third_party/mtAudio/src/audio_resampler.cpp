#include "audio_resampler.h"
#include <cstring>

namespace mtAudio {

// ── 重采样目标参数 ──
static constexpr int kTargetSampleRate   = 16000;
static constexpr int kTargetChannels     = 1;
static constexpr int kTargetBitsPerSample = 16;

AudioResampler::AudioResampler()
{
    memset(&converter_, 0, sizeof(converter_));
}

AudioResampler::~AudioResampler()
{
    cleanup();
}

ma_format AudioResampler::toMaFormat(int bits)
{
    switch (bits) {
    case 8:  return ma_format_u8;
    case 16: return ma_format_s16;
    case 24: return ma_format_s24;
    case 32: return ma_format_s32;
    default: return ma_format_unknown;
    }
}

bool AudioResampler::init(int inSampleRate, int inChannels, int inBitsPerSample)
{
    cleanup();

    inSampleRate_    = inSampleRate;
    inChannels_      = inChannels;
    inBitsPerSample_ = inBitsPerSample;

    // 检查是否需要重采样（已匹配目标格式）
    if (inSampleRate == kTargetSampleRate && inChannels == kTargetChannels
        && inBitsPerSample == kTargetBitsPerSample) {
        needsResample_  = false;
        initialized_    = true;
        return true;
    }

    needsResample_ = true;

    ma_data_converter_config cfg = ma_data_converter_config_init(
        toMaFormat(inBitsPerSample), toMaFormat(kTargetBitsPerSample),
        inChannels, kTargetChannels,
        inSampleRate, kTargetSampleRate);

    ma_result result = ma_data_converter_init(&cfg, nullptr, &converter_);
    if (result != MA_SUCCESS) {
        initialized_ = false;
        return false;
    }

    initialized_ = true;
    return true;
}

size_t AudioResampler::estimateOutputSize(size_t inputSize) const
{
    size_t bytesPerInputFrame = ma_get_bytes_per_frame(
        toMaFormat(inBitsPerSample_), inChannels_);
    if (bytesPerInputFrame == 0) return 0;

    ma_uint64 inputFrames = inputSize / bytesPerInputFrame;
    ma_uint64 outputFrames = 0;
    ma_result r = ma_data_converter_get_expected_output_frame_count(
        &converter_, inputFrames, &outputFrames);
    if (r != MA_SUCCESS) return 0;

    return outputFrames * ma_get_bytes_per_frame(
        toMaFormat(kTargetBitsPerSample), kTargetChannels);
}

bool AudioResampler::resample(const uint8_t* inData, size_t inSize,
                              std::vector<uint8_t>& outData)
{
    if (!initialized_ || !inData || inSize == 0) return false;

    // 无需重采样 → 直接复制
    if (!needsResample_) {
        outData.assign(inData, inData + inSize);
        return true;
    }

    // 预估输出大小并分配缓冲区（加余量防止边界不足）
    size_t estSize = estimateOutputSize(inSize);
    if (estSize == 0) estSize = inSize * 2;  // fallback
    outData.resize(estSize + 256);

    size_t bytesPerInputFrame = ma_get_bytes_per_frame(
        toMaFormat(inBitsPerSample_), inChannels_);
    ma_uint64 framesIn  = inSize / bytesPerInputFrame;
    ma_uint64 framesOut = estSize / ma_get_bytes_per_frame(
        toMaFormat(kTargetBitsPerSample), kTargetChannels);

    const void* pInput  = static_cast<const void*>(inData);
    void*       pOutput = static_cast<void*>(outData.data());

    ma_result result = ma_data_converter_process_pcm_frames(
        &converter_, pInput, &framesIn, pOutput, &framesOut);

    if (result != MA_SUCCESS) {
        outData.clear();
        return false;
    }

    // 按实际输出大小裁剪
    size_t actualBytes = framesOut * ma_get_bytes_per_frame(
        toMaFormat(kTargetBitsPerSample), kTargetChannels);
    outData.resize(actualBytes);

    return true;
}

void AudioResampler::reset()
{
    if (initialized_ && needsResample_) {
        ma_data_converter_reset(&converter_);
    }
}

void AudioResampler::cleanup()
{
    if (initialized_ && needsResample_) {
        ma_data_converter_uninit(&converter_, nullptr);
    }
    initialized_   = false;
    needsResample_ = true;
    memset(&converter_, 0, sizeof(converter_));
}

}  // namespace mtAudio
