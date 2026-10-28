#include "render_scheduler.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <chrono>
#include <thread>

#include <w_log_cpp.h>
extern "C" {
#include <libavutil/frame.h>
}

namespace muxplay {

RenderScheduler::RenderScheduler(IFrameConsumer* consumer, MediaClock* clock,
                                 SyncStrategy* strategy,
                                 std::mutex* cvMutex,
                                 std::condition_variable* frameCv)
    : consumer_(consumer)
    , clock_(clock)
    , strategy_(strategy)
    , cvMutex_(cvMutex)
    , frameCv_(frameCv)
    , scheduledWallTime_(std::chrono::steady_clock::now())
{}

bool RenderScheduler::waitForFrame(
    moodycamel::ReaderWriterQueue<AVFrame*>& frameQueue,
    std::atomic<bool>& stopFlag)
{
    std::unique_lock<std::mutex> lock(*cvMutex_);
    frameCv_->wait_for(lock, std::chrono::milliseconds(10), [&] {
        return frameQueue.size_approx() > 0 || stopFlag;
    });
    // 更新壁钟追踪，防止空等被误计为 wallGap
    clock_->lastDequeueWallGapMs = 0;
    // 方案3：不重置 scheduledWallTime_。
    // 原实现 scheduledWallTime_ = now 会把空等期间的时间静默吞掉，
    // 导致抖动恢复后视频永久落后于源 PTS、节奏不可追回。
    // 保留旧基准后，下一轮 sleepUntil(scheduledWallTime_ + interval) 会
    // 因 target < now 立即返回，自动连续渲染积压帧追平进度，
    // 节拍基准只向前推进、不向后拉。
    return !stopFlag;
}

void RenderScheduler::sleepUntil(MediaClock::SteadyTime target) {
    while (true) {
        auto now = std::chrono::steady_clock::now();
        if (now >= target) break;
        auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
            target - now);
        if (remaining.count() <= 0) break;
        int64_t chunk = std::min<int64_t>(remaining.count(), 5);
        std::this_thread::sleep_for(std::chrono::milliseconds(chunk));
    }
}

void RenderScheduler::run(
    moodycamel::ReaderWriterQueue<AVFrame*>& frameQueue,
    std::atomic<bool>& stopFlag)
{
    auto lastDequeueTime = std::chrono::steady_clock::now();
    int fast_max_count = 0;
    while (!stopFlag) {
        // ── 出队 ──
        AVFrame* frame = nullptr;
        if (!frameQueue.try_dequeue(frame)) {
            if (!waitForFrame(frameQueue, stopFlag)) break;
            continue;
        }

        // ── wallGap 诊断 ──
        auto dequeueNow = std::chrono::steady_clock::now();
        clock_->lastDequeueWallGapMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            dequeueNow - lastDequeueTime).count();
        lastDequeueTime = dequeueNow;

        // ── 更新时钟 ──
        clock_->pendingVideoPtsMs = ptsCalculator_ ? ptsCalculator_(frame) : frame->pts;
        clock_->pendingFrameCount = frameQueue.size_approx();

        // ── Live 模式：帧到达即渲染，跳过 PTS 调度和 sleep ──
        if (live_mode_) {
            if (!firstFrameFired_ && firstFrameCb_) {
                firstFrameFired_ = true;
                firstFrameCb_();
            }
            if (preRenderCb_) {
                preRenderCb_(frame);
            }
            consumer_->consume(frame);
            clock_->lastVideoPtsMs = clock_->pendingVideoPtsMs;
            clock_->lastVideoWallTime = dequeueNow;
            av_frame_free(&frame);
            frameCv_->notify_one();
            continue;
        }

        // ── PTS 壁钟直驱模式：首帧锚定 ──
        if (clock_->master == ClockMaster::PTS && clock_->anchorPtsMs < 0) {
            clock_->anchorPtsMs = clock_->pendingVideoPtsMs;
            clock_->anchorWallTime = std::chrono::steady_clock::now();
            scheduledWallTime_ = clock_->anchorWallTime;
            LOG_DEBUG("[scheduler] [uuid={}] PTS 壁钟锚定: pts={}ms",
                uuid_, (long long)clock_->anchorPtsMs);
        }

        // ── 帧率自适应：根据相邻帧 PTS 差值更新帧间隔 ──
        if (clock_->lastVideoPtsMs >= 0) {
            int64_t ptsGap = clock_->pendingVideoPtsMs - clock_->lastVideoPtsMs;
            if (ptsGap > 0 && ptsGap < 1000) {
                clock_->frameIntervalMs = static_cast<int>(ptsGap);
            }
            // PTS 跳变/seek 检测：重新锚定
            if (ptsGap < -500 || ptsGap > 5000) {
                clock_->lastVideoPtsMs = -1;
                clock_->anchorPtsMs = -1;
                scheduledWallTime_ = dequeueNow;
            }
        }

        // ── 决策 ──
        Decision d = strategy_->decide(*clock_);

        // ── 执行 ──
        switch (d.action) {
        case RenderDecision::DROP: {
            consecutiveDrop_++;
            totalDropped_++;
            fast_max_count = 0;
            if((consecutiveDrop_>2 && consecutiveDrop_%3==0) || (totalDropped_>=100 && totalDropped_%100==0)){
                LOG_WARN("[scheduler] [uuid={}] 视频pts落后->连续丢帧:{}次, 共丢帧={}, videoPts={}ms, audioDelta={}ms, q={}, wallGap={}ms, reason={}",
                    uuid_,
                    consecutiveDrop_,
                    totalDropped_,
                    (long long)clock_->pendingVideoPtsMs,
                    (long long)clock_->videoAudioDeltaMs(),
                    clock_->pendingFrameCount,
                    (long long)clock_->lastDequeueWallGapMs,
                    d.reason);
            }
            clock_->lastVideoPtsMs = clock_->pendingVideoPtsMs;
            clock_->lastVideoWallTime = dequeueNow;
            scheduledWallTime_ = dequeueNow;
            av_frame_free(&frame);
            frameCv_->notify_one();
            continue;
        }

        case RenderDecision::WAIT: {
            fast_max_count++;
            // 视频严重超前 → sleep 等待音频追上，然后正常渲染本帧
            int64_t aheadMs = clock_->videoAudioDeltaMs();
            int sleepMul = fast_max_count > 4? 2 : 1;
            int64_t waitMs = std::min<int64_t>(aheadMs, 50*sleepMul);
            if(fast_max_count>4 && fast_max_count%4==0){
                LOG_WARN("[scheduler] [uuid={}] 视频连续:{}次严重超前, waitMs={}ms, audioDelta={}ms, reason={}",
                    uuid_,
                    fast_max_count,
                    (long long)waitMs,
                    (long long)aheadMs,
                    d.reason);
            }
            if(fast_max_count>20){
                LOG_WARN("发生多次连续同步问题,无法扭转;AV同步模式降级为PTS 壁钟直驱");
                clock_->master = ClockMaster::PTS;
            }
            sleepUntil(std::chrono::steady_clock::now() + std::chrono::milliseconds(waitMs));
            consecutiveDrop_ = 0;
            scheduledWallTime_ = std::chrono::steady_clock::now();
            consumer_->consume(frame);
            clock_->lastVideoPtsMs = clock_->pendingVideoPtsMs;
            clock_->lastVideoWallTime = scheduledWallTime_;
            break;
        }

        case RenderDecision::RENDER_FAST:
        case RenderDecision::RENDER_NORMAL:
        case RenderDecision::RENDER_SLOW: {
            consecutiveDrop_ = 0;
            fast_max_count = 0;

            // ── 壁钟调度 ──
            if (clock_->master == ClockMaster::PTS && clock_->anchorPtsMs >= 0) {
                // PTS 壁钟直驱：目标壁钟 = 锚点壁钟 + (当前PTS - 锚点PTS)
                // 帧到早了 → targetWall > now → sleepUntil 等待（上限 100ms）
                // 帧到晚了 → targetWall < now → sleepUntil 立即返回，自动追赶
                auto targetWall = clock_->anchorWallTime +
                    std::chrono::milliseconds(
                        clock_->pendingVideoPtsMs - clock_->anchorPtsMs);
                auto now = std::chrono::steady_clock::now();
                if (targetWall > now) {
                    auto waitMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                        targetWall - now).count();
                    if (waitMs > 100) {
                        targetWall = now + std::chrono::milliseconds(100);
                    }
                }
                scheduledWallTime_ = targetWall;
            } else {
                // AV 同步模式：以调度时间累加 interval
                scheduledWallTime_ = scheduledWallTime_ +
                    std::chrono::milliseconds(d.intervalMs);
            }
            sleepUntil(scheduledWallTime_);

            // ── 首帧回调 ──
            if (!firstFrameFired_ && firstFrameCb_) {
                firstFrameFired_ = true;
                firstFrameCb_();
            }

            // ── 预渲染回调（截图/录制等）──
            if (preRenderCb_) {
                preRenderCb_(frame);
            }

            lastRenderedPtsMs_ = clock_->pendingVideoPtsMs;
            // ── 渲染 ──
            consumer_->consume(frame);

            // ── 更新时钟 ──
            clock_->lastVideoPtsMs = clock_->pendingVideoPtsMs;
            clock_->lastVideoWallTime = scheduledWallTime_;

            break;
        }
        }

        av_frame_free(&frame);
        frameCv_->notify_one();
    }

    // ── 退出统计 ──
    if (totalDropped_ > 0) {
        LOG_INFO("[scheduler] [uuid={}]渲染调度器线程退出, 统计丢包数 = {}", uuid_, (long long)totalDropped_);
    }
    else{
        LOG_DEBUG("[scheduler] [uuid={}]渲染调度器线程退出", uuid_);
    }
}

} // namespace muxplay
