#pragma once

/**
 * @file audio_player_internal.h
 * @brief mtAudioPlayer 内部实现类声明（包内可见）
 */

#include "../include/mt_audio_player.h"

#include <miniaudio.h>

#include <atomic>
#include <deque>
#include <mutex>
#include <vector>

namespace mtAudio {

// ── miniaudio 后端播放器 ──
class MiniaudioPlayer : public IAudioPlayer {
public:
    MiniaudioPlayer();
    ~MiniaudioPlayer() override;

    bool init(int sampleRate, int channels, int bitsPerSample) override;
    bool pushFrame(const AudioFrame& frame) override;
    void play() override;
    void pause() override;
    void stop() override;
    void setVolume(float vol) override;
    bool isPlaying() const override;
    int64_t getPlayingPts() const override;

private:
    /// 停止并销毁 ma_device
    void closeDevice();

    /// ma_device 数据回调（静态 → 转发到实例方法）
    static void dataCallback(ma_device* pDevice, void* pOutput, const void* pInput,
                             ma_uint32 frameCount);

    /// 实例级回调实现
    void onDataCallback(void* pOutput, ma_uint32 frameCount);

    // ── PTS 标记 ──
    struct PtsEntry {
        int64_t pts;
        int     numFrames;  // 帧数（= samples / channels）
    };

    /// 从 PTS 队列头部消费指定帧数，返回对应的 PTS
    int64_t consumePts(int frames);

    // ── miniaudio 对象 ──
    ma_device        device_{};
    ma_device_config deviceConfig_{};
    ma_pcm_rb        ringBuffer_{};   // 无锁环形缓冲区（线程安全）
    bool             deviceInited_ = false;

    // ── PTS 跟踪 ──
    mutable std::mutex    ptsMutex_;
    std::deque<PtsEntry>  ptsQueue_;
    std::atomic<int64_t>  currentPts_{-1};

    // ── 状态 ──
    std::atomic<bool> playing_{false};
    std::atomic<bool> stopped_{false};
    float             volume_ = 1.0f;
};

}  // namespace mtAudio
