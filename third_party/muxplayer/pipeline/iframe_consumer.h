#pragma once

#include "frame_types.h"
#include <chrono>
#include <functional>

namespace muxplay {

// ── 帧消费器抽象 ──
// 运行在独立的 consumer 线程中，从 frame 队列取帧并处理。
// HW 路径：在此线程做 D3D11 渲染
// SW 路径：在此线程做轻量帧替换（FramePump），GL 渲染由 display 线程完成
class IFrameConsumer {
public:
    virtual ~IFrameConsumer() = default;

    // 消费一帧（consumer 线程调用）。
    // 调用者会负责 av_frame_free，实现只需处理帧数据。
    virtual void consume(AVFrame* frame) = 0;

    // 帧渲染完成时的回调注册（跨线程通知 display 刷新）
    virtual void setOnFrameReady(std::function<void()> cb) = 0;

    // 设置 player uuid
    virtual void setPlayerUUid(const std::string& uuid){ uuid_ = uuid; };

protected:
    std::string uuid_;
};

} // namespace muxplay
