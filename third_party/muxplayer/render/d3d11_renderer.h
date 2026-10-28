#pragma once

#include "../pipeline/iframe_consumer.h"
#include <d3d11.h>
#include <functional>
#include <mutex>

namespace muxplay {

class Nv12TextureShare;
class DxRender;
class OverlayManager;

// ── D3D11 帧渲染器（IFrameConsumer 实现）──
// 在 consumer 线程调用，持有 d3dMutex_ 做 D3D11 渲染操作。
// 支持两种子模式：
//   GL 互通模式：使用 Nv12TextureShare（NV12 Copy + PS → 共享 RGBA）
//   SwapChain 模式：使用 DxRender（完整 D3D11 管线 + Present）
class D3D11FrameRenderer : public IFrameConsumer {
public:
    explicit D3D11FrameRenderer(HWND hwnd);
    ~D3D11FrameRenderer() override;

    // 初始化渲染器（必须在 open 过程中调用）
    // d3dMutex: 指向 mux_private_->d3dMutex_，用于 D3D11 线程安全
    bool init(ID3D11Device* device, ID3D11DeviceContext* context,
              std::mutex* d3dMutex, uint32_t width, uint32_t height, bool useGLInterop);

    // ── IFrameConsumer 接口 ──
    void consume(AVFrame* frame) override;
    void setOnFrameReady(std::function<void()> cb) override;

    // 销毁渲染资源
    void destroy();

    // 设置 Overlay 管理器（由外部注入，生命周期由外部管理）
    void setOverlayManager(OverlayManager* mgr) { overlayMgr_ = mgr; }

    // ── 画中画（PIP）接口 ──
    void enablePip(bool enabled);
    bool isPipEnabled() const;
    void setPipRect(const RECT& rect);
    void setDpiScale(float scale);

    // ── SwapChain resize（由 MultiplexPlayer::resizeRender 调用，需在 d3dMutex 内）──
    void resizeToRect(uint32_t width, uint32_t height);

    // 获取共享纹理（供 WGL interop display 使用）
    ID3D11Texture2D* sharedTexture() const;

private:
    HWND hwnd_ = nullptr;

    // 两种子渲染器（互斥使用）
    Nv12TextureShare* nv12Share_ = nullptr;   // GL 互通模式
    DxRender*         render_    = nullptr;   // SwapChain 模式
    bool              useGLInterop_ = false;

    // D3D11 设备引用（由外部管理生命周期）
    ID3D11Device*        d3dDevice_  = nullptr;
    ID3D11DeviceContext* d3dContext_ = nullptr;
    std::mutex*          d3dMutex_   = nullptr;  // 指向 mux_private_->d3dMutex_

    // Overlay 管理器（由外部注入）
    OverlayManager*      overlayMgr_ = nullptr;

    // 帧就绪回调（跨线程通知 display 刷新）
    std::function<void()> onFrameReady_;
    std::mutex            callbackMutex_;
};

} // namespace muxplay
