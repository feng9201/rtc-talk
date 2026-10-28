# renderLoop 音视频同步架构重设计

## 2. 新架构：三层模型

```
┌─────────────────────────────────────────────────┐
│                  MediaClock                      │
│  videoPts  ← 已渲染末帧 PTS                      │
│  audioPts  ← 当前音频播放位置                     │
│  wallTime  ← 壁钟参考                            │
│  master    ← 以谁为准（audio）                    │
└──────────────┬──────────────────────────────────┘
               │  query(delta)
               ▼
┌─────────────────────────────────────────────────┐
│              SyncStrategy                        │
│  decide(videoPts, audioPts) → Decision           │
│                                                   │
│  Decision 枚举:                                   │
│    RENDER_NORMAL   // 正常渲染，保持帧率           │
│    RENDER_FAST     // 加速渲染，压缩等待           │
│    RENDER_SLOW     // 减速渲染，拉伸等待           │
│    DROP            // 丢弃本帧                     │
│    WAIT            // 等待更多帧（queue 空）       │
└──────────────┬──────────────────────────────────┘
               │  Decision
               ▼
┌─────────────────────────────────────────────────┐
│              RenderScheduler                     │
│  执行 decision:                                   │
│    DROP       → av_frame_free + continue         │
│    RENDER_*   → consumer_->consume + sleep(Xms)  │
│    WAIT       → CV wait                          │
│  更新 MediaClock.videoPts                         │
└─────────────────────────────────────────────────┘
```

- **MediaClock**：纯数据，所有组件通过它读写时间，不再散落全局变量
- **SyncStrategy**：**纯函数**，输入时钟状态，输出决策枚举。可替换、可单元测试、可调参
- **RenderScheduler**：执行决策，驱动渲染。不再自己做判断

---

## 3. 组件设计

### 3.1 MediaClock

```cpp
struct MediaClock {
    // ── 视频侧 ──
    int64_t lastVideoPtsMs = -1;       // 上次已渲染帧的 PTS
    SteadyTime lastVideoWallTime;      // 上次已渲染帧的壁钟时间
    int64_t pendingVideoPtsMs = -1;    // 当前等待处理的帧 PTS

    // ── 音频侧 ──
    std::function<int64_t()> audioPtsCallback;  // 外部注入
    int  syncToleranceMs = 200;

    // ── 队列 ──
    size_t pendingFrameCount = 0;      // decoded_frame_queue 深度

    // ── 诊断 ──
    int64_t lastDequeueWallGapMs = 0;  // 上次出队与上上次的壁钟差

    // 查询
    int64_t videoAudioDeltaMs() const {
        if (pendingVideoPtsMs < 0 || !audioPtsCallback) return 0;
        return pendingVideoPtsMs - audioPtsCallback();
    }
};
```

### 3.2 SyncStrategy

```cpp
enum class RenderDecision {
    RENDER_NORMAL,   // 正常帧率渲染
    RENDER_FAST,     // 加速（sleep = 帧间隔 - accelerateMs）
    RENDER_SLOW,     // 减速（sleep = 帧间隔 + decelerateMs）
    DROP,            // 丢弃帧
    WAIT,            // 队列空，等待
};

struct SyncParams {
    int toleranceMs = 200;           // 容差
    int fastCapMs   = 30;            // 单帧最大加速
    int slowCapMs   = 5;             // 单帧最大减速
    int gopGapThresholdMs = 80;      // wallGap > 此值视为 GOP 边界
    int gopCompensateMs    = 25;      // GOP 边界额外补偿
};

class SyncStrategy {
public:
    explicit SyncStrategy(const SyncParams& params);

    // 核心：纯函数，无副作用
    RenderDecision decide(const MediaClock& clock);

    // 获取当前渲染间隔（ms），RenderScheduler 据此 sleep
    int  renderIntervalMs() const { return intervalMs_; }

    // 本次决策的解释字符串（日志用）
    const char* reason() const { return reason_; }

private:
    SyncParams params_;
    int  intervalMs_ = 0;
    const char* reason_ = "";
};
```

**决策算法**：

```cpp
RenderDecision SyncStrategy::decide(const MediaClock& clock) {
    int64_t delta = clock.videoAudioDeltaMs();
    int tol = params_.toleranceMs;

    // ── 1. 视频严重落后 → 丢弃 ──
    //  滞后超过容差 + 1帧间隔 + 安全边际 → 加速追不上
    int dropThreshold = -(tol + clock.frameIntervalMs() + params_.fastCapMs);
    if (delta < dropThreshold) {
        intervalMs_ = 0;
        reason_ = "DROP";
        return RenderDecision::DROP;
    }

    // ── 2. 视频明显落后（在容差内）→ 加速渲染 ──
    if (delta < -(tol / 3)) {   // ~-67ms
        int accelerate = std::min<int>((-delta) / 3, params_.fastCapMs);
        intervalMs_ = clock.frameIntervalMs() - accelerate;
        reason_ = "RENDER_FAST";
        return RenderDecision::RENDER_FAST;
    }

    // ── 3. 视频轻微落后 → 微加速（死区避免抖动）──
    if (delta < -20) {
        int accelerate = std::min<int>((-delta) / 5, params_.fastCapMs / 2);
        intervalMs_ = clock.frameIntervalMs() - accelerate;
        reason_ = "RENDER_FAST";
        return RenderDecision::RENDER_FAST;
    }

    // ── 4. 视频超前 → 减速渲染 ──
    if (delta > tol) {
        int decelerate = std::min<int>(delta / 20, params_.slowCapMs);
        intervalMs_ = clock.frameIntervalMs() + decelerate;
        reason_ = "RENDER_SLOW";
        return RenderDecision::RENDER_SLOW;
    }

    // ── 5. 同步良好 → 正常渲染 ──
    intervalMs_ = clock.frameIntervalMs();
    reason_ = "RENDER_NORMAL";
    return RenderDecision::RENDER_NORMAL;
}
```

### 3.3 RenderScheduler

```cpp
class RenderScheduler {
public:
    RenderScheduler(IFrameConsumer* consumer, MediaClock* clock,
                    SyncStrategy* strategy);

    // 主循环（替代当前 renderLoop）
    void run(moodycamel::ReaderWriterQueue<AVFrame*>& frameQueue,
             std::atomic<bool>& stopFlag);

private:
    IFrameConsumer* consumer_;
    MediaClock*     clock_;
    SyncStrategy*   strategy_;

    // 统计
    int64_t totalDropped_ = 0;
    int  consecutiveDrop_ = 0;
};
```

**主循环**：

```cpp
void RenderScheduler::run(Queue& frameQueue, atomic<bool>& stop) {
    while (!stop) {
        // ── 出队 ──
        AVFrame* frame = nullptr;
        if (!frameQueue.try_dequeue(frame)) {
            // queue 空，CV 等待
            waitForFrame(frameQueue, stop);
            continue;
        }

        // ── 更新时钟 ──
        clock_->pendingVideoPtsMs = computePts(frame);
        clock_->pendingFrameCount = frameQueue.size_approx();

        // ── 决策 ──
        RenderDecision d = strategy_->decide(*clock_);

        // ── 执行 ──
        switch (d) {
        case DROP:
            consecutiveDrop_++;
            totalDropped_++;
            logDrop(frame, clock_);
            clock_->lastVideoPtsMs = clock_->pendingVideoPtsMs;
            clock_->lastVideoWallTime = now();
            av_frame_free(&frame);
            continue;

        case RENDER_FAST:
        case RENDER_NORMAL:
        case RENDER_SLOW:
            consecutiveDrop_ = 0;
            // 等待到目标壁钟时间
            sleepUntil(clock_->lastVideoWallTime +
                       ms(strategy_->renderIntervalMs()));
            consumer_->consume(frame);
            clock_->lastVideoPtsMs = clock_->pendingVideoPtsMs;
            clock_->lastVideoWallTime = now();
            break;

        case WAIT:
            // 不应到达此处（出队成功时不会 WAIT）
            break;
        }

        av_frame_free(&frame);
        notifyDecoder(frameQueue);
    }

    logExitStats(totalDropped_);
}
```

---

## 4. GOP 边界的特殊处理

GOP 边界的特点是：queue 从满（15 帧）瞬间变空，壁钟空等 ~100-150ms，然后新 GOP 的 15 帧一起入队。

**SyncStrategy 内部**已经通过 `delta` 感知到滞后，不需要额外的 "GOP 边界检测"：

```
GOP 间隙 126ms:
  音频前进了 126ms，视频 PTS 没变
  delta = -126ms → 落在 "视频明显落后" 分支
  加速 42ms/帧 × 3 帧 = 126ms 回收 → 同步恢复

正常播放时:
  delta ≈ 0 → "同步良好" 分支，正常帧率
```

**唯一需要显式处理的是**：GOP 间隙结束后，queue 突然有 15 帧可用。此时调度器可以连续 RENDER_FAST 追赶，不需要线程间协调。

---

## 5. 对比

| 维度 | 当前架构 | 新架构 |
|------|---------|--------|
| 时钟 | 散落在 `lastFramePtsMs_` / `lastFrameWallTime_` / callback | `MediaClock` 统一封装 |
| 同步决策 | if/else 分支操作 `delayMs` | `SyncStrategy::decide()` 纯函数输出枚举 |
| 帧率控制 | `delayMs = frameInterval - elapsed` | `RenderScheduler::sleepUntil(targetWallTime)` |
| 可测试性 | 无法独立测试 | `SyncStrategy` 可输入 mock clock 单元测试 |
| 可替换性 | 改代码 | 换 `SyncStrategy` 实现即可（策略模式） |
| GOP 边界 | 靠 wallGap > 80 间接感知 | 靠 `delta` 自然感知 |

---

## 6. 文件变更清单

| 文件 | 操作 | 内容 |
|------|------|------|
| `muxPlayer/pipeline/media_clock.h` | 新增 | `MediaClock` 结构体 |
| `muxPlayer/pipeline/sync_strategy.h` | 新增 | `RenderDecision` 枚举 + `SyncStrategy` 类声明 |
| `muxPlayer/pipeline/sync_strategy.cpp` | 新增 | `SyncStrategy::decide()` 实现 |
| `muxPlayer/pipeline/render_scheduler.h` | 新增 | `RenderScheduler` 类声明 |
| `muxPlayer/pipeline/render_scheduler.cpp` | 新增 | `RenderScheduler::run()` 主循环 |
| `muxPlayer/multiplex_player.cpp` | 修改 | `renderLoop()` 替换为 `RenderScheduler::run()` 调用 |
| `muxPlayer/multiplex_player_private.h` | 修改 | 添加 `MediaClock` / `SyncStrategy` / `RenderScheduler` 成员 |
| `muxPlayer/CMakeLists.txt` | 修改 | 添加新源文件 |

---

## 7. 迁移路径

1. **第一步**：新增 `media_clock.h`、`sync_strategy.h/.cpp`、`render_scheduler.h/.cpp`，不碰现有代码
2. **第二步**：在 `multiplex_player_private.h` 添加新成员，构造时初始化
3. **第三步**：`renderLoop()` 替换为 `scheduler_->run()`，删除旧 AV 同步代码
4. **第四步**：编译验证，跑测对比跳帧率

---

## 8. 期望效果

| 指标 | 当前 | 目标 |
|------|------|------|
| 每 GOP 跳帧数 | 3 帧 | 0 帧 |
| 累计跳帧/10分钟 | 700+ | < 10（仅网络异常时） |
| 音画同步偏差 | ±288ms 波动 | ±60ms 稳态 |
| 代码复杂度 | renderLoop ~200 行单体 | 3 个 < 100 行的独立组件 |

---

## 9. 无音频模式：PTS 时钟 + 抖动缓冲渲染

### 9.1 问题

当前 AV 同步架构的核心输入是 `videoAudioDeltaMs()`，它依赖 `audioPtsCallback`。无音频流时该回调为空，`delta` 恒为 0，所有同步策略退化为 `RENDER_NORMAL`。唯一的无音频路径 `live_mode_` 是"帧到即渲"，不处理网络抖动。

**无音频场景的实际需求**：
- 网络摄像头 RTSP 子码流（无音频轨）
- FLV 纯视频拉流
- 网络抖动导致帧到达间隔在 10ms~200ms 之间剧烈波动
- 期望：平缓渲染，不卡顿，允许可控延迟换取流畅度

### 9.2 核心思路

```
无音频时，用 PTS 时间线 + 壁钟锚点 替代 音频时钟：

   anchorWallTime ────────────────────────────→ 壁钟
        │
        │  (pts - anchorPts) = 期望经过的时间
        │
   anchorPts ────────────────────────────────→ PTS 时间线
                 帧①   帧②    帧③   帧④
```

- **PTS 锚点**：首帧渲染时，记录 `(anchorPtsMs, anchorWallTime)`，建立 PTS ↔ 壁钟映射
- **期望 PTS**：`expectedPtsMs = (now - anchorWallTime) + anchorPtsMs`，即"壁钟过了多久，PTS 就该走多远"
- **Delta**：`delta = pendingPtsMs - expectedPtsMs`，完全替代 `videoAudioDeltaMs()`
- **抖动缓冲**：渲染前先积攒 `minBufferFrames` 帧，吸收网络到达间隔的方差

### 9.3 时钟主控枚举

```cpp
enum class ClockMaster {
    Audio,   // 以音频 PTS 为基准（现有模式）
    PTS,     // 以 PTS + 壁钟锚点为基准（无音频模式）
    None,    // 无主控时钟（live 模式，帧到即渲）
};
```

`MediaClock` 新增字段：

```cpp
struct MediaClock {
    // ...existing fields...

    ClockMaster master = ClockMaster::Audio;  // 时钟主控源

    // ── PTS 锚点（仅 ClockMaster::PTS 时有效）──
    int64_t    anchorPtsMs     = -1;    // 首帧 PTS
    SteadyTime anchorWallTime;          // 首帧壁钟

    // ── 抖动缓冲（仅 ClockMaster::PTS 时有效）──
    int  minBufferFrames = 3;           // 开始渲染前至少积攒的帧数
    int  maxBufferFrames = 8;           // 超过此值触发丢帧

    // ── 查询：返回 pendingPts - 期望 PTS ──
    int64_t ptsDeltaMs() const {
        if (pendingVideoPtsMs < 0 || anchorPtsMs < 0) return 0;
        auto now = std::chrono::steady_clock::now();
        int64_t elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - anchorWallTime).count();
        int64_t expectedPtsMs = anchorPtsMs + elapsedMs;
        return pendingVideoPtsMs - expectedPtsMs;
    }
};
```

### 9.4 SyncStrategy 扩展

`SyncStrategy::decide()` 根据 `clock.master` 选择不同的 delta 源：

```cpp
Decision SyncStrategy::decide(const MediaClock& clock) const {
    int64_t delta = 0;

    switch (clock.master) {
    case ClockMaster::Audio:
        delta = clock.videoAudioDeltaMs();   // 现有逻辑
        break;
    case ClockMaster::PTS:
        delta = clock.ptsDeltaMs();          // PTS vs 壁钟锚点
        break;
    case ClockMaster::None:
        return Decision::normal(clock.frameIntervalMs);  // live 模式不决策
    }

    // ...后续 delta → Decision 的映射逻辑完全不变...
}
```

**无音频时参数差异**（可在 `SyncParams` 中扩展或新增 `NoAudioSyncParams`）：

| 参数 | AV 模式 | 无音频模式 | 原因 |
|------|---------|-----------|------|
| `toleranceMs` | 200 | 80 | 无音频时钟漂移，容差可以更紧 |
| `fastCapMs` | 30 | 20 | 防止过度加速导致画面跳跃 |
| `slowCapMs` | 5 | 10 | 网络抖动需要更强的减速能力 |
| `dropThreshold` | `-(tol + interval/2)` | `-(tol * 3)` | 无音频时不丢帧优先，等待缓冲恢复 |

### 9.5 RenderScheduler 扩展：PTS 壁钟直驱

**设计理念**：不需要状态机、不需要抖动缓冲、不需要冻帧管理。每帧出队后，用 PTS 锚点直接计算它"该在壁钟上出现的时刻"，`sleepUntil` 到那个时刻即可。

```
首帧渲染时锚定：
    anchorPts = frame.pts
    anchorWall = now

之后每帧出队后：
    targetWall = anchorWall + (frame.pts - anchorPts)

    targetWall > now → sleepUntil(targetWall)   帧到早了，等
    targetWall ≤ now → 立即渲染                  帧到晚了，追
```

**为什么这就够了**：

| 场景 | 行为 | 效果 |
|------|------|------|
| 帧匀速到达 | `targetWall ≈ now`，sleep ≈ 0 | 正常帧率 |
| 服务端缓存 GOP 瞬间涌入 | 前几帧 `targetWall > now`，sleep 等待 | 自然限速，不积压 |
| 网络抖动帧到晚了 | `targetWall < now`，立即渲染 | 自动追赶，不丢帧 |
| 网络断流 | `waitForFrame` 阻塞 | 恢复后 `targetWall < now` 连续追帧 |
| seek/PTS 跳变 | 重新锚定 | 无缝衔接 |

抖动不是靠"攒帧缓冲"消除的，而是靠 `sleepUntil` 的壁钟调度自然吸收——帧到早了就等，到晚了就追。

**RenderScheduler 无新增成员**（复用已有 `scheduledWallTime_`），RENDER 分支改动：

```cpp
case RenderDecision::RENDER_FAST:
case RenderDecision::RENDER_NORMAL:
case RenderDecision::RENDER_SLOW: {
    // ── 壁钟调度 ──
    if (clock_->master == ClockMaster::PTS && clock_->anchorPtsMs >= 0) {
        // PTS 壁钟直驱：目标壁钟 = 锚点壁钟 + (当前PTS - 锚点PTS)
        scheduledWallTime_ = clock_->anchorWallTime +
            std::chrono::milliseconds(
                clock_->pendingVideoPtsMs - clock_->anchorPtsMs);
    } else {
        // AV 同步模式：以调度时间累加 interval
        scheduledWallTime_ = scheduledWallTime_ +
            std::chrono::milliseconds(d.intervalMs);
    }
    sleepUntil(scheduledWallTime_);
    // ...渲染...
}
```

首帧锚定在出队后、决策前完成：

```cpp
// ── PTS 壁钟直驱模式：首帧锚定 ──
if (clock_->master == ClockMaster::PTS && clock_->anchorPtsMs < 0) {
    clock_->anchorPtsMs = clock_->pendingVideoPtsMs;
    clock_->anchorWallTime = std::chrono::steady_clock::now();
    scheduledWallTime_ = clock_->anchorWallTime;
}
```

seek/PTS 跳变时重置锚点：

```cpp
if (ptsGap < -500 || ptsGap > 5000) {
    clock_->lastVideoPtsMs = -1;
    clock_->anchorPtsMs = -1;  // 下次出队时重新锚定
    scheduledWallTime_ = dequeueNow;
}
```

**与三段式状态机方案的对比**：

| 维度 | 三段式状态机（已废弃） | PTS 壁钟直驱 |
|------|----------------------|-------------|
| 状态机 | BUFFERING/PLAYING/STARVING | **无** |
| 冻帧管理 | clone/释放/周期重渲染 | **无** |
| 自适应缓冲 | EMA 统计 + 每30帧调整 | **无** |
| 丢帧逻辑 | 极端积压丢帧 | **无**（sleepUntil 自然限速） |
| seek 处理 | 重置 bufferState | 重置锚点 |
| 代码量 | ~100 行状态机 | **3 行** 公式 |
| bug 数 | 5 个 | 0（无状态可出错） |

### 9.6 与现有三种模式的关系

| 模式 | `master` | 触发条件 | 延迟 | 适用场景 |
|------|----------|---------|------|---------|
| **AV Sync** | `Audio` | `audioPtsCallback` 有效 | ~40ms | 有音频的 RTSP/文件播放 |
| **PTS 直驱** | `PTS` | 无音频 + 网络流 | ~0ms（首帧即渲） | 无音频 RTSP/FLV 拉流 |
| **Live** | `None` | 无音频 + 本地设备 | <5ms | USB 摄像头、采集卡 |

### 9.7 音频中途出现时的模式切换

RTSP 流可能先只有视频，后续动态加入音频轨。切换流程：

```
1. 音频轨就绪 → audioPtsCallback 可用
2. 用 audioPts 重新计算 anchor:
   anchorPtsMs = audioPtsCallback()
   anchorWallTime = now
3. clock.master = ClockMaster::Audio
4. 下一轮 decide() 自动使用 videoAudioDeltaMs()
```

切换平滑的关键：在音频就绪的瞬间，`audioPts ≈ pendingVideoPtsMs`（两者来自同源 PTS），所以切换后 `delta ≈ 0`，不会产生瞬时丢帧或卡顿。

### 9.8 决策参数对比总表

```
                    AV Sync          无音频 (PTS)        Live
                   ─────────        ─────────────      ─────
delta 源           videoAudioDelta  不使用              不使用
时钟基准           音频 PTS          PTS 壁钟锚点        无
sleepUntil 目标    scheduledWall + interval  anchorWall + (pts - anchorPts)  无
toleranceMs        200              -                   -
fastCapMs          30               -                   -
slowCapMs          50               -                   -
dropThreshold      严格             无丢帧              无丢帧
缓冲区管理         无               无（sleepUntil 吸收） 无
首帧延迟           0                0                   0
```

### 9.9 文件变更清单（增量）

| 文件 | 操作 | 内容 |
|------|------|------|
| `media_clock.h` | 修改 | +`ClockMaster` 枚举，+`anchorPtsMs`/`anchorWallTime` |
| `sync_strategy.cpp` | 修改 | `decide()` 中 PTS 模式直接返回 `RENDER_NORMAL` |
| `render_scheduler.h` | 修改 | +`setClockMaster()`（无新增状态/缓冲成员） |
| `render_scheduler.cpp` | 修改 | RENDER 分支加入 PTS 壁钟直驱公式 + 首帧锚定 |
| `multiplex_player.cpp` | 修改 | 无音频流时设置 `master = PTS` |
