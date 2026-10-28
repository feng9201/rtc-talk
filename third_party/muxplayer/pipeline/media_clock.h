#pragma once

#include <chrono>
#include <cstdint>
#include <functional>

namespace muxplay {

// ── 时钟主控源 ──
enum class ClockMaster {
    Audio,   // 以音频 PTS 为基准（AV 同步模式）
    PTS,     // 以 PTS + 壁钟锚点为基准（无音频抖动缓冲模式）
    None,    // 无主控时钟（Live 模式，帧到即渲）
};

// ── 统一媒体时钟 ──
// 所有组件通过它读写时间，不再散落全局变量。
// 纯数据结构，无副作用。
struct MediaClock {
    using SteadyTime = std::chrono::steady_clock::time_point;

    // ── 视频侧 ──
    int64_t    lastVideoPtsMs      = -1;  // 上次已渲染帧的 PTS（毫秒）
    SteadyTime lastVideoWallTime;         // 上次已渲染帧的壁钟时间
    int64_t    pendingVideoPtsMs   = -1;  // 当前等待处理的帧 PTS（毫秒）

    // ── 音频侧 ──
    std::function<int64_t()> audioPtsCallback;  // 外部注入，返回当前音频播放位置（毫秒）
    int syncToleranceMs = 200;                   // AV 同步容差

    // ── 帧率 ──
    int frameIntervalMs = 40;  // 帧间隔（毫秒），默认 25fps，运行时更新

    // ── 队列 ──
    size_t pendingFrameCount = 0;  // decoded_frame_queue 深度

    // ── 诊断 ──
    int64_t lastDequeueWallGapMs = 0;  // 上次出队与上上次的壁钟差（毫秒）

    // ── 时钟主控 ──
    ClockMaster master = ClockMaster::Audio;  // 默认 AV 同步模式

    // ── PTS 锚点（仅 ClockMaster::PTS 时有效）──
    int64_t    anchorPtsMs     = -1;    // 首帧 PTS（毫秒）
    SteadyTime anchorWallTime;          // 首帧壁钟

    // ── 初始化/重置 ──
    // 将所有成员恢复为默认值（含清除音频回调与壁钟锚点）
    void init() {
        lastVideoPtsMs     = -1;
        lastVideoWallTime  = SteadyTime{};
        pendingVideoPtsMs  = -1;
        audioPtsCallback   = nullptr;
        syncToleranceMs    = 200;
        frameIntervalMs    = 40;
        pendingFrameCount  = 0;
        lastDequeueWallGapMs = 0;
        master             = ClockMaster::Audio;
        anchorPtsMs        = -1;
        anchorWallTime     = SteadyTime{};
    }

    // ── 查询 ──
    // 返回 pendingVideoPtsMs - audioPts，正值=视频超前，负值=视频落后
    int64_t videoAudioDeltaMs() const {
        if (pendingVideoPtsMs < 0 || !audioPtsCallback) return 0;
        int64_t audioPts = audioPtsCallback();
        if (audioPts < 0) return 0;
        return pendingVideoPtsMs - audioPts;
    }

    // 返回 pendingPts - 期望 PTS（基于壁钟锚点），正值=视频超前，负值=视频落后
    int64_t ptsDeltaMs() const {
        if (pendingVideoPtsMs < 0 || anchorPtsMs < 0) return 0;
        auto now = std::chrono::steady_clock::now();
        int64_t elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - anchorWallTime).count();
        int64_t expectedPtsMs = anchorPtsMs + elapsedMs;
        return pendingVideoPtsMs - expectedPtsMs;
    }
};

} // namespace muxplay
