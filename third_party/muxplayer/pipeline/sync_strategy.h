#pragma once

#include "media_clock.h"

namespace muxplay {

// ── 渲染决策枚举 ──
enum class RenderDecision {
    RENDER_NORMAL,   // 正常帧率渲染
    RENDER_FAST,     // 加速渲染（压缩帧间隔）
    RENDER_SLOW,     // 减速渲染（拉伸帧间隔）
    DROP,            // 丢弃本帧（视频严重落后）
    WAIT,            // 等待（队列空 或 视频严重超前）
};

// ── 决策结果 ──
struct Decision {
    RenderDecision action;
    int  intervalMs;   // 目标帧间隔（毫秒），DROP/WAIT 时为 0
    const char* reason; // 日志用

    static Decision normal(int intervalMs) {
        return { RenderDecision::RENDER_NORMAL, intervalMs, "NORMAL" };
    }
    static Decision fast(int intervalMs, const char* reason = "FAST") {
        return { RenderDecision::RENDER_FAST, intervalMs, reason };
    }
    static Decision slow(int intervalMs, const char* reason = "SLOW") {
        return { RenderDecision::RENDER_SLOW, intervalMs, reason };
    }
    static Decision drop(const char* reason = "DROP") {
        return { RenderDecision::DROP, 0, reason };
    }
    static Decision wait(const char* reason = "WAIT") {
        return { RenderDecision::WAIT, 0, reason };
    }
};

// ── 同步策略参数 ──
struct SyncParams {
    int toleranceMs       = 200;   // AV 同步容差
    int fastCapMs         = 30;    // 单帧最大加速量（毫秒）
    int slowCapMs         = 50;     // 单帧最大减速量（毫秒）
    int wallGapThresholdMs = 100;  // wallGap > 此值视为 GOP 边界，额外加速补偿
    int wallGapCompensateMs = 30;  // GOP 边界额外加速量（毫秒）
};

// ── 同步策略 ──
// 纯函数：输入 MediaClock，输出 Decision。无副作用，可单元测试。
class SyncStrategy {
public:
    explicit SyncStrategy(const SyncParams& params = SyncParams{});

    // 设置无音频模式参数（当 clock.master == ClockMaster::PTS 时生效）
    void setNoAudioParams(const SyncParams& params) { noAudioParams_ = params; }

    // 核心决策：根据时钟状态决定本帧如何处理
    Decision decide(const MediaClock& clock) const;

private:
    SyncParams params_;
    SyncParams noAudioParams_{
        80,   // toleranceMs - 无音频容差更紧
        20,   // fastCapMs - 防止过度加速
        10,   // slowCapMs - 网络抖动需要更强减速
        100,  // wallGapThresholdMs
        30    // wallGapCompensateMs
    };
};

} // namespace muxplay
