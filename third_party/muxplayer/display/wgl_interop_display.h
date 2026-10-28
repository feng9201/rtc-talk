#pragma once

#include "../pipeline/idisplay_consumer.h"
#include "../include/gl_interop.h"
#include <QOpenGLFunctions_3_3_Core>
#include <functional>

struct ID3D11Texture2D;

namespace muxplay {

// ── WGL NV_DX_interop 显示策略 ──
// 实现 IDisplayConsumer，将 D3D11 共享 RGBA 纹理通过 WGL interop 渲染到 OpenGL。
// 使用回调获取纹理和 D3D 锁，不直接依赖 MultiplexPlayer。
class WGLInteropDisplay : public IDisplayConsumer {
public:
    using TextureProvider = std::function<ID3D11Texture2D*()>;
    using D3DLockFn       = std::function<void()>;

    // texProvider: 获取当前 D3D11 共享 RGBA 纹理
    // lock/unlock:  D3D11 ImmediateContext 线程安全锁
    WGLInteropDisplay(TextureProvider texProvider,
                      D3DLockFn lock, D3DLockFn unlock);
    ~WGLInteropDisplay() override;

    // ── IDisplayConsumer 接口 ──
    // 初始化和渲染均在 GL context 激活的线程调用
    bool initGL(QOpenGLFunctions_3_3_Core* gl);   // 编译 shader / 创建 quad 几何 / GL 纹理（由 initializeGL 调用）
    void paint(int viewportW, int viewportH) override;
    void clear() override;
    void notifyNewFrame() override;
    bool isReady() const override;

private:
    bool compileShaders();
    void createQuadGeometry();
    void renderQuad(int viewportW, int viewportH, int texW, int texH);

    TextureProvider     texProvider_;
    D3DLockFn           d3dLock_;
    D3DLockFn           d3dUnlock_;

    QOpenGLFunctions_3_3_Core* gl_ = nullptr;
    GLInterop           glInterop_;
    bool                glInited_    = false;

    // ── OpenGL 资源 ──
    GLuint  program_    = 0;
    GLuint  vao_        = 0;
    GLuint  vbo_        = 0;
    GLuint  glTexture_  = 0;
    int     texWidth_   = 0;
    int     texHeight_  = 0;
};

} // namespace muxplay
