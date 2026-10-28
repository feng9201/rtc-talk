#pragma once

#include "text_renderer.h"
#include "image_renderer.h"
#include "shader_manager.h"
#include <d3d11.h>
#include <mutex>
#include <vector>
#include <memory>

#ifdef ENABLE_WGL_INTEROP
class QPainter;
#endif

namespace muxplay {

// ── Overlay 抽象管理层 ──
// 管理 TextRenderer / ImageRenderer 的生命周期，提供 D3D 和 Qt 两种渲染路径。
// 线程安全：overlay 列表操作由内部 mutex 保护。
// D3D 渲染在 d3dMutex_ 锁内调用，Qt 渲染在 GL 线程调用。
class OverlayManager {
public:
    OverlayManager();
    ~OverlayManager();

    // ── 生命周期 ──
    // 在 D3D 设备就绪后调用（open 成功后），初始化 ShaderManager
    bool initD3D(ID3D11Device* device);
    void destroy();

    // ── 高 DPI 缩放 ──
    // 设置逻辑像素→物理像素的缩放比（Qt AA_EnableHighDpiScaling 场景）
    // 必须在 createTextOverlay/createImageOverlay 之前调用
    void setDpiScale(float scale) { dpiScale_ = scale; }
    float getDpiScale() const { return dpiScale_; }

    // ── Text Overlay API（线程安全）──
    int  createTextOverlay(const char* text, const char* fontName, int fontSize);
    void destroyTextOverlay(int id);
    bool updateTextOverlay(int id, const char* text);
    bool setTextOverlayRect(int id, const RECT& rect);
    bool setTextOverlayColor(int id, unsigned int color);

    // ── Image Overlay API（线程安全）──
    int  createImageOverlay(int width, int height, const void* bgraData, int stride);
    void destroyImageOverlay(int id);
    bool setImageOverlayRect(int id, const RECT& rect);
    bool setImageOverlayColorTint(int id, unsigned int colorTint);

    // ── D3D 渲染（render 线程，在 d3dMutex_ 锁内调用）──
    // 按创建顺序绘制所有 overlay（视频 quad 之后调用）
    void drawD3D(ID3D11DeviceContext* ctx, int viewportW, int viewportH);

#ifdef ENABLE_WGL_INTEROP
    // ── Qt 渲染（GL 线程，在 paintGL 中调用）──
    // 用 QPainter 绘制所有 overlay（GL 互通路径专用）
    void drawQt(QPainter* painter, int viewportW, int viewportH);
#endif

private:
    TextRenderer*  findText(int id);
    ImageRenderer* findImage(int id);

    std::mutex mutex_;   // 保护 overlay 列表
    std::vector<std::unique_ptr<TextRenderer>>  texts_;
    std::vector<std::unique_ptr<ImageRenderer>> images_;
    int nextTextId_  = 1;
    int nextImageId_ = 1;

    // D3D 资源
    ShaderManager shaders_;
    ID3D11Device* d3dDevice_ = nullptr;
    bool d3dReady_ = false;

    // 采样器状态（overlay 专用）
    Microsoft::WRL::ComPtr<ID3D11SamplerState> samplerState_;

    // Alpha 混合状态（overlay 专用，确保文字/图片背景透明）
    Microsoft::WRL::ComPtr<ID3D11BlendState> blendState_;

    // 高 DPI 缩放比（逻辑像素→物理像素，默认 1.0 = 96 DPI）
    float dpiScale_ = 1.0f;
};

} // namespace muxplay
