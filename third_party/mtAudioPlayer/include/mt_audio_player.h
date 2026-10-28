#pragma once

/**
 * @file mt_audio_player.h
 * @brief mtAudioPlayer 音频播放库 —— 基于 miniaudio 的 PCM 播放
 *
 * 设计原则：
 *   - 接口可扩展（IAudioPlayer 抽象基类），当前后端为 miniaudio
 *   - 调用方通过 pushFrame() 喂入解码后的 PCM 帧
 *   - 内部使用环形缓冲区平滑播放，非阻塞推送
 *   - 支持音视频同步：getPlayingPts() 返回当前正在播放的音频 PTS
 *
 * 使用方式：
 *   auto player = IAudioPlayer::create(IAudioPlayer::Backend::Miniaudio);
 *   player->init(16000, 1, 16);
 *   player->play();
 *   // 在回调中喂数据:
 *   player->pushFrame(audioFrame);
 */

#include "mt_audio_decoder.h"  // AudioFrame
#include <cstdint>
#include <memory>
#include <functional>

namespace mtAudio {

// ── 音频播放器抽象基类 ──
class IAudioPlayer {
public:
    virtual ~IAudioPlayer() = default;

    /// 播放后端类型
    enum class Backend {
        Miniaudio = 0,  // miniaudio 后端（当前唯一实现）
    };

    /// 工厂方法
    static std::unique_ptr<IAudioPlayer> create(Backend backend = Backend::Miniaudio);

    /// 初始化播放器
    /// @param sampleRate    输出采样率 (Hz)
    /// @param channels      声道数
    /// @param bitsPerSample 位深 (8/16/24/32)
    /// @return 是否成功
    virtual bool init(int sampleRate, int channels, int bitsPerSample = 16) = 0;

    /// 推送一帧解码后的 PCM 数据（非阻塞）
    /// 若内部缓冲区满则丢弃帧，返回 false
    /// @return 是否成功推入播放队列
    virtual bool pushFrame(const AudioFrame& frame) = 0;

    // ── 播放控制 ──
    virtual void play() = 0;
    virtual void pause() = 0;
    virtual void stop() = 0;

    // ── 音量控制 ──
    /// @param vol 0.0 ~ 1.0
    virtual void setVolume(float vol) = 0;

    // ── 状态查询 ──
    virtual bool isPlaying() const = 0;

    /// 获取当前正在播放的音频 PTS（毫秒），用于音视频同步
    /// @return PTS 毫秒值，无有效值为 -1
    virtual int64_t getPlayingPts() const = 0;

protected:
    int sampleRate_    = 0;
    int channels_      = 0;
    int bitsPerSample_ = 0;
};

}  // namespace mtAudio
