#pragma once

#include "media_clock.h"
#include "sync_strategy.h"
#include "iframe_consumer.h"

#include <atomic>
#include <cstdint>
#include <condition_variable>
#include <functional>
#include <mutex>

#include <spsc/readerwriterqueue.h>

namespace muxplay {

// ── 渲染调度器 ──
// 执行 SyncStrategy 的决策，驱动渲染循环。
// 替代当前 MultiplexPlayer::renderLoop()。
class RenderScheduler {
public:
    RenderScheduler(IFrameConsumer* consumer, MediaClock* clock,
                    SyncStrategy* strategy,
                    std::mutex* cvMutex,
                    std::condition_variable* frameCv);

    // 设置 PTS 计算器（将 AVFrame 转为毫秒 PTS）
    using PtsCalculator = std::function<int64_t(AVFrame*)>;
    void setPtsCalculator(PtsCalculator calc) { ptsCalculator_ = std::move(calc); }

    // 设置每帧渲染前的回调（用于截图、录制等）
    using PreRenderCallback = std::function<void(AVFrame* frame)>;
    void setPreRenderCallback(PreRenderCallback cb) { preRenderCb_ = std::move(cb); }

    // 设置首帧渲染回调
    using FirstFrameCallback = std::function<void()>;
    void setFirstFrameCallback(FirstFrameCallback cb) { firstFrameCb_ = std::move(cb); }

    // 设置 Live 模式（帧到达即渲染，跳过 PTS 调度和 sleep）
    void setLiveMode(bool v) { live_mode_ = v; }

    // 设置时钟主控源
    void setClockMaster(ClockMaster master) { clock_->master = master; }

    // 主循环（替代当前 renderLoop）
    void run(moodycamel::ReaderWriterQueue<AVFrame*>& frameQueue,
             std::atomic<bool>& stopFlag);

    // 统计
    int64_t totalDropped() const { return totalDropped_; }

    void setUuid(const std::string& uuid) { uuid_ = uuid; }
private:
    // 等待帧队列非空（CV wait，替代忙等）
    bool waitForFrame(moodycamel::ReaderWriterQueue<AVFrame*>& frameQueue,
                      std::atomic<bool>& stopFlag);

    // 分片 sleep，支持快速退出
    void sleepUntil(MediaClock::SteadyTime target);

    IFrameConsumer* consumer_;
    MediaClock*     clock_;
    SyncStrategy*   strategy_;

    PtsCalculator ptsCalculator_;

    std::mutex*              cvMutex_;
    std::condition_variable* frameCv_;

    // 统计
    int64_t totalDropped_     = 0;
    int     consecutiveDrop_  = 0;
    int64_t lastRenderedPtsMs_ = -1;

    // 壁钟追踪（用于 sleepUntil）
    MediaClock::SteadyTime scheduledWallTime_;

    // 回调
    PreRenderCallback  preRenderCb_;
    FirstFrameCallback firstFrameCb_;
    bool               firstFrameFired_ = false;

    // Live 模式（无音频的本地摄像头场景）
    bool live_mode_ = false;

    std::string uuid_;
};

} // namespace muxplay
