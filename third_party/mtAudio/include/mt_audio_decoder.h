#pragma once

/**
 * @file mt_audio_decoder.h
 * @brief mtAudio 音频解码库 —— 统一解码 AAC / Opus / G.711
 *
 * 使用方式：
 *   auto dec = AudioDecoder::create(AudioCodec::AAC);
 *   dec->init(extraData, extraLen, sampleRate, channels);
 *   AudioFrame frame;
 *   dec->decode(packetData, packetLen, frame, pts);
 *
 * 重采样开关（CMake宏 MT_AUDIO_ENABLE_RESAMPLE，默认关闭）：
 *   开启后输出统一重采样为 16000Hz / 单声道 / 16-bit
 *   若输入格式已满足则不进行重采样
 */

#include <cstdint>
#include <memory>
#include <vector>

namespace mtAudio {

// ── 音频编解码器类型 ──
enum class AudioCodec {
    AAC,     // MPEG-4 AAC (LC)
    Opus,    // Opus
    G711A,   // G.711 A-law
    G711U,   // G.711 μ-law
};

// ── 解码后的 PCM 帧 ──
struct AudioFrame {
    std::vector<uint8_t> data;   // PCM 原始数据
    int   sampleRate    = 0;     // 采样率 (Hz)
    int   channels      = 0;     // 声道数
    int   bitsPerSample = 0;     // 位深 (8/16/24/32)
    int   numSamples    = 0;     // 每声道样本数
    int64_t pts         = -1;    // 展示时间戳 (毫秒)，无 PTS 时为 -1

    bool empty() const { return data.empty(); }
};

// ── 音频解码器抽象基类 ──
class AudioDecoder {
public:
    virtual ~AudioDecoder() = default;

    /// 工厂方法：按编解码类型创建解码器
    static std::unique_ptr<AudioDecoder> create(AudioCodec codec);

    /// 初始化解码器（解码 AAC/Opus 前必须调用）
    /// @param extraData   extradata（AAC: AudioSpecificConfig; Opus: 可为空）
    /// @param extraDataLen extradata 长度
    /// @param sampleRate  采样率（AAC/Opus 若传 0 则由 extradata 推断）
    /// @param channels    声道数
    virtual bool init(const uint8_t* extraData, int extraDataLen,
                      int sampleRate = 0, int channels = 1) = 0;

    /// 解码一帧压缩音频数据 → PCM
    /// @param data    压缩数据指针
    /// @param len     压缩数据长度
    /// @param outFrame 输出 PCM 帧
    /// @param pts     展示时间戳 (毫秒)，无 PTS 传 -1
    /// @return        每声道样本数，< 0 表示错误
    virtual int decode(const uint8_t* data, int len,
                       AudioFrame& outFrame, int64_t pts = -1) = 0;

    /// 重置解码器内部状态（seek/切换流后调用）
    virtual void reset() = 0;

    /// 获取解码器输出采样率
    int outputSampleRate() const { return outputSampleRate_; }
    /// 获取解码器输出声道数
    int outputChannels() const { return outputChannels_; }

protected:
    int outputSampleRate_ = 0;
    int outputChannels_ = 1;
};

}  // namespace mtAudio
