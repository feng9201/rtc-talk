#include "d3d11_renderer.h"
#include "nv12_share.h"
#include "dx_render.h"
#include "overlay_manager.h"
#include <d3d11.h>
#include <w_log_cpp.h>

extern "C" {
#include <libavutil/pixfmt.h>
}


namespace muxplay {

D3D11FrameRenderer::D3D11FrameRenderer(HWND hwnd)
    : hwnd_(hwnd)
{
}

D3D11FrameRenderer::~D3D11FrameRenderer()
{
    destroy();
}

bool D3D11FrameRenderer::init(ID3D11Device* device, ID3D11DeviceContext* context,
                               std::mutex* d3dMutex, uint32_t width, uint32_t height, bool useGLInterop)
{
    if (!device || !context || !d3dMutex) return false;

    d3dDevice_  = device;
    d3dContext_ = context;
    d3dMutex_   = d3dMutex;
    useGLInterop_ = useGLInterop;

    if (useGLInterop_) {
        // GL 互通模式：NV12 拷贝 + PS → 共享 RGBA
        if (!nv12Share_) {
            nv12Share_ = new Nv12TextureShare();
            nv12Share_->setUUid(uuid_);
        }
        return nv12Share_->init(device, width, height);
    } else {
        // 传统 SwapChain 模式
        if (!render_) {
            render_ = new DxRender(hwnd_);
            render_->setUUid(uuid_);
        }
        return render_->InitRender(width, height, device, context);
    }
}

void D3D11FrameRenderer::consume(AVFrame* frame)
{
    if (!frame) return;

    bool needPresent = false;
    {
        std::lock_guard<std::mutex> lock(*d3dMutex_);

        if (useGLInterop_ && nv12Share_) {
            if (frame->format == AV_PIX_FMT_D3D11) {
                // 硬解：解码器纹理 → NV12 拷贝 → PS NV12→RGBA → 共享纹理
                ID3D11Texture2D* decoderTex = (ID3D11Texture2D*)(intptr_t)frame->data[0];
                int ySubresource = (int)(intptr_t)frame->data[1];
                nv12Share_->render(d3dContext_, decoderTex, ySubresource);
            } else {
                // 软解降级：CPU 帧 → 上传 NV12 → PS NV12→RGBA → 共享纹理
                nv12Share_->renderFromCPU(d3dContext_, frame);
            }
        } else if (render_) {
            // 传统 SwapChain 渲染（GPU 命令在锁内提交）
            render_->renderVideo(frame);
            // Overlay 叠加绘制（视频 quad 之后、Present 之前）
            if (overlayMgr_) {
                overlayMgr_->drawD3D(render_->context(),
                                     render_->renderWidth(),
                                     render_->renderHeight());
            }
            needPresent = true;  // Present 在锁外执行，避免阻塞解码线程
        }
    }

    // ── V-Sync Present 在锁外执行，不阻塞 decoderLoop 的 d3dMutex_ 获取 ──
    if (needPresent) {
        render_->present();
    }

    // 通知 display 有新帧就绪
    std::lock_guard<std::mutex> cbLock(callbackMutex_);
    if (onFrameReady_) {
        onFrameReady_();
    }
}

void D3D11FrameRenderer::setOnFrameReady(std::function<void()> cb)
{
    std::lock_guard<std::mutex> lock(callbackMutex_);
    onFrameReady_ = std::move(cb);
}

void D3D11FrameRenderer::destroy()
{
    LOG_INFO("[uuid={}] d3d11 渲染器 destroy", uuid_);
    if (nv12Share_) {
        nv12Share_->destroy();
        delete nv12Share_;
        nv12Share_ = nullptr;
    }
    if (render_) {
        render_->destroy();
        delete render_;
        render_ = nullptr;
    }
    d3dDevice_  = nullptr;
    d3dContext_ = nullptr;
}

ID3D11Texture2D* D3D11FrameRenderer::sharedTexture() const
{
    if (nv12Share_) {
        return nv12Share_->sharedTexture();
    }
    if (render_) {
        return render_->getSharedTexture();
    }
    return nullptr;
}

// ── PIP 画中画接口转发 ──
void D3D11FrameRenderer::enablePip(bool enabled)
{
    if (render_) render_->enablePip(enabled);
}

bool D3D11FrameRenderer::isPipEnabled() const
{
    return render_ ? render_->isPipEnabled() : false;
}

void D3D11FrameRenderer::setPipRect(const RECT& rect)
{
    if (render_) render_->setPipRect(rect);
}

void D3D11FrameRenderer::setDpiScale(float scale)
{
    if (render_) render_->setDpiScale(scale);
}

void D3D11FrameRenderer::resizeToRect(uint32_t width, uint32_t height)
{
    if (render_) render_->ResizeToRect(width, height);
}

} // namespace muxplay
