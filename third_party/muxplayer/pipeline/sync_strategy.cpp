#include "sync_strategy.h"
#include <algorithm>
#include <cstdio>

namespace muxplay {

SyncStrategy::SyncStrategy(const SyncParams& params)
    : params_(params) {}

Decision SyncStrategy::decide(const MediaClock& clock) const {
    // 根据时钟主控源选择 delta 和参数
    int64_t delta = 0;
    int tol = params_.toleranceMs;
    int fastCap = params_.fastCapMs;
    int slowCap = params_.slowCapMs;
    int wallGapThreshold = params_.wallGapThresholdMs;
    int wallGapCompensate = params_.wallGapCompensateMs;

    switch (clock.master) {
    case ClockMaster::Audio:
        delta = clock.videoAudioDeltaMs();
        break;
    case ClockMaster::PTS:
        // PTS 壁钟直驱：不参与 delta 决策，由 RenderScheduler 用锚点公式直接算 sleepUntil 目标
        return Decision::normal(clock.frameIntervalMs);
    case ClockMaster::None:
        // Live 模式：不做决策，正常帧率渲染
        return Decision::normal(clock.frameIntervalMs);
    }

    int frameInterval = clock.frameIntervalMs;

    // ── 1. 视频严重落后 → 丢弃 ──
    // 无音频时放宽丢帧阈值（tol * 3），优先等待缓冲恢复
    int dropThreshold = (clock.master == ClockMaster::PTS)
        ? -(tol * 3)
        : -(tol + frameInterval / 2);
    if (delta < dropThreshold) {
        return Decision::drop("DROP: video behind");
    }

    // ── 2. 视频严重超前 → 等待 ──
    if (delta > tol * 2) {
        return Decision::wait("WAIT: video ahead");
    }

    // ── 3. 视频明显落后（在容差内）→ 加速渲染 ──
    if (delta < -(tol / 3)) {
        int accelerate = std::min<int>(static_cast<int>((-delta) / 3), fastCap);
        int interval = frameInterval - accelerate;
        return Decision::fast(interval, "FAST: catching up");
    }

    // ── 4. 视频轻微落后 → 微加速（死区避免抖动）──
    if (delta < -20) {
        int accelerate = std::min<int>(static_cast<int>((-delta) / 5), fastCap / 2);
        int interval = frameInterval - accelerate;
        return Decision::fast(interval, "FAST: micro adjust");
    }

    // ── 5. 视频超前 → 减速渲染 ──
    if (delta > tol) {
        int decelerate = std::min<int>(static_cast<int>(delta / 5), slowCap);
        int interval = frameInterval + decelerate;
        return Decision::slow(interval, "SLOW: waiting");
    }

    // ── 6. GOP 边界补偿 / 抖动恢复：wallGap 大表示刚经历空等期 ──
    if (clock.lastDequeueWallGapMs > wallGapThreshold) {
        int extra = std::min<int>(static_cast<int>(clock.lastDequeueWallGapMs / 5),
                                  wallGapCompensate);
        int interval = frameInterval - extra;
        return Decision::fast(interval, "FAST: jitter recovery");
    }

    // ── 7. 同步良好 → 正常渲染 ──
    return Decision::normal(frameInterval);
}

} // namespace muxplay
