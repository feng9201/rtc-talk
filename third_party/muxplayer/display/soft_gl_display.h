#pragma once

#include "../pipeline/idisplay_consumer.h"
#include <QOpenGLFunctions_3_3_Core>
#include <functional>
#include <memory>

struct AVFrame;

namespace muxplay {

class GLYuvShader;
class FramePump;

// ── 软件 GL 显示策略 ──
// 实现 IDisplayConsumer，从 FramePump 获取 CPU 侧 YUV 帧，
// 通过 GLYuvShader 上传到 GL 纹理并渲染 YUV→RGB。
class SoftGLDisplay : public IDisplayConsumer {
public:
    // frameProvider: 回调返回最新解码帧（由 FramePump::acquireLatestFrame 提供）
    using FrameProvider = std::function<AVFrame*()>;

    explicit SoftGLDisplay(FrameProvider frameProvider);
    ~SoftGLDisplay() override;

    // 初始化 GL 资源（由 initializeGL 调用，GL context 已激活）
    bool initGL(QOpenGLFunctions_3_3_Core* gl);

    // ── IDisplayConsumer 接口 ──
    void paint(int viewportW, int viewportH) override;
    void clear() override;
    void notifyNewFrame() override;
    bool isReady() const override;

private:
    FrameProvider frameProvider_;

    QOpenGLFunctions_3_3_Core* gl_ = nullptr;
    std::unique_ptr<GLYuvShader> yuvShader_;
    bool glInited_ = false;
};

} // namespace muxplay
