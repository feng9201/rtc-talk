#pragma once

#include "../pipeline/iframe_consumer.h"
#include <functional>
#include <mutex>

namespace muxplay {

// ── 软解帧泵（IFrameConsumer 实现）──
// 在 consumer 线程运行，仅做轻量帧替换：
//   1. 用最新解码帧替换 latestSwFrame_
//   2. 触发 display 刷新回调
// 实际的 GL 渲染由 SoftGLDisplay 在 GL 线程完成。
class FramePump : public IFrameConsumer {
public:
    FramePump();
    ~FramePump() override;

    // ── IFrameConsumer 接口 ──
    void consume(AVFrame* frame) override;
    void setOnFrameReady(std::function<void()> cb) override;

    // ── 帧访问（GL 线程调用）──
    // 返回最新帧的引用计数副本，调用者负责 av_frame_free
    AVFrame* acquireLatestFrame();
    void Clear();

private:
    std::mutex mutex_;
    AVFrame*   latestFrame_ = nullptr;

    std::function<void()> onFrameReady_;
    std::mutex            callbackMutex_;
};

} // namespace muxplay
