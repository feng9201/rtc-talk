#pragma once

namespace muxplay {

// ── 显示消费者抽象 ──
// 在 GL 线程（paintGL）中调用，负责将已准备好的帧渲染到屏幕。
// HW 路径 (WGLInteropDisplay)：锁定 D3D11 共享纹理 → GL 渲染 → 解锁
// SW 路径 (SoftGLDisplay)：          取 CPU 帧 → YUV 上传 GL → YUV→RGB shader
class IDisplayConsumer {
public:
    virtual ~IDisplayConsumer() = default;

    // 渲染一帧（GL 线程调用，当前 OpenGL context 已激活）
    virtual void paint(int viewportW, int viewportH) = 0;

    // 清屏为黑色（播放停止 / 错误时调用）
    virtual void clear() = 0;

    // 新帧就绪通知（跨线程，由 frame consumer 触发 display 刷新）
    virtual void notifyNewFrame() = 0;

    // 设置 GLPlayWidget 尺寸变更时的降级模式
    virtual bool isReady() const = 0;
};

} // namespace muxplay
