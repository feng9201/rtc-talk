#pragma once

/**
 * @file audio_resampler.h
 * @brief 基于 miniaudio 的 PCM 重采样封装
 *
 * 目标格式（MT_AUDIO_ENABLE_RESAMPLE 开启时）：
 *   16000Hz / 单声道 / 16-bit signed
 */

#include <cstdint>
#include <string>
#include <vector>

#include <miniaudio.h>

namespace mtAudio {

class AudioResampler {
public:
    AudioResampler();
    ~AudioResampler();

    AudioResampler(const AudioResampler&) = delete;
    AudioResampler& operator=(const AudioResampler&) = delete;

    /// 初始重采样器
    /// @return true 成功
    bool init(int inSampleRate, int inChannels, int inBitsPerSample);

    /// 重采样 PCM 数据到 16000Hz/mono/16-bit
    /// @param inData   输入 PCM 数据
    /// @param inSize   输入数据字节数
    /// @param outData  输出 PCM 数据
    /// @return true 成功（若输入已为目标格式则不处理，直接复制）
    bool resample(const uint8_t* inData, size_t inSize,
                  std::vector<uint8_t>& outData);

    /// 检查输入格式是否已满足目标格式（无需重采样）
    bool needsResample() const { return needsResample_; }

    /// 重置转换器状态
    void reset();

private:
    void cleanup();

    // 格式转换：bits -> ma_format
    static ma_format toMaFormat(int bits);
    // 计算输出数据大小（预估）
    size_t estimateOutputSize(size_t inputSize) const;

    int  inSampleRate_    = 0;
    int  inChannels_      = 0;
    int  inBitsPerSample_ = 0;
    bool needsResample_   = true;
    bool initialized_     = false;

    ma_data_converter converter_;
};

}  // namespace mtAudio
