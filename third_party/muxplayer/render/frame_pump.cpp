#include "frame_pump.h"

namespace muxplay {

FramePump::FramePump() = default;

FramePump::~FramePump()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (latestFrame_) {
        av_frame_free(&latestFrame_);
    }
}

void FramePump::consume(AVFrame* frame)
{
    if (!frame) return;

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (latestFrame_) {
            av_frame_free(&latestFrame_);
        }
        // 制作引用计数副本（调用者 renderLoop 负责释放原始 frame）
        latestFrame_ = av_frame_alloc();
        if (latestFrame_) {
            av_frame_ref(latestFrame_, frame);
        }
    }

    // 通知 display 有新帧
    std::lock_guard<std::mutex> cbLock(callbackMutex_);
    if (onFrameReady_) {
        onFrameReady_();
    }
}

void FramePump::setOnFrameReady(std::function<void()> cb)
{
    std::lock_guard<std::mutex> lock(callbackMutex_);
    onFrameReady_ = std::move(cb);
}

AVFrame* FramePump::acquireLatestFrame()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!latestFrame_) return nullptr;

    // 返回引用计数副本（av_frame_ref），调用者负责 av_frame_free
    AVFrame* copy = av_frame_alloc();
    if (av_frame_ref(copy, latestFrame_) < 0) {
        av_frame_free(&copy);
        return nullptr;
    }
    return copy;
}


void FramePump::Clear()
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (latestFrame_) {
        av_frame_free(&latestFrame_);
    }
}
} // namespace muxplay
