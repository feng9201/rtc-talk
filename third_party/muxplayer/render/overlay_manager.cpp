#include "overlay_manager.h"
#include <cstdio>

#ifdef ENABLE_WGL_INTEROP
#include <QPainter>
#include <QFont>
#include <QImage>
#endif

#define OVERLAY_LOG(fmt, ...) do { fprintf(stderr, "[OverlayManager] " fmt "\n", ##__VA_ARGS__); fflush(stderr); } while(0)

namespace muxplay {

OverlayManager::OverlayManager()
{
}

OverlayManager::~OverlayManager()
{
    destroy();
}

bool OverlayManager::initD3D(ID3D11Device* device)
{
    if (!device) return false;
    if (d3dReady_) return true;

    d3dDevice_ = device;

    if (!shaders_.Init(device)) {
        OVERLAY_LOG("ShaderManager Init FAILED");
        return false;
    }

    // 创建 overlay 专用采样器状态
    D3D11_SAMPLER_DESC sampDesc = {};
    sampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
    sampDesc.MinLOD = 0;
    sampDesc.MaxLOD = D3D11_FLOAT32_MAX;
    HRESULT hr = device->CreateSamplerState(&sampDesc, samplerState_.GetAddressOf());
    if (FAILED(hr)) {
        OVERLAY_LOG("CreateSamplerState FAILED hr=0x%08X", (unsigned)hr);
        return false;
    }

    d3dReady_ = true;

    // ── 创建 Alpha 混合状态（文字/图片 overlay 背景透明）──
    D3D11_BLEND_DESC blendDesc = {};
    blendDesc.RenderTarget[0].BlendEnable = TRUE;
    blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
    blendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    hr = device->CreateBlendState(&blendDesc, blendState_.GetAddressOf());
    if (FAILED(hr)) {
        OVERLAY_LOG("CreateBlendState FAILED hr=0x%08X", (unsigned)hr);
        return false;
    }

    OVERLAY_LOG("Init OK");
    return true;
}

void OverlayManager::destroy()
{
    std::lock_guard<std::mutex> lock(mutex_);
    images_.clear();
    texts_.clear();
    samplerState_.Reset();
    blendState_.Reset();
    shaders_ = ShaderManager();
    d3dDevice_ = nullptr;
    d3dReady_ = false;
}

// ── Text Overlay API ──

int OverlayManager::createTextOverlay(const char* text, const char* fontName, int fontSize)
{
    if (!d3dReady_ || !text || !fontName) return -1;

    auto renderer = std::make_unique<TextRenderer>(d3dDevice_, &shaders_, dpiScale_);
    if (!renderer->Create(text, fontName, fontSize)) return -1;

    int id = nextTextId_++;
    renderer->SetId(id);

    std::lock_guard<std::mutex> lock(mutex_);
    texts_.push_back(std::move(renderer));
    return id;
}

void OverlayManager::destroyTextOverlay(int id)
{
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto it = texts_.begin(); it != texts_.end(); ++it) {
        if ((*it)->GetId() == id) {
            texts_.erase(it);
            return;
        }
    }
}

bool OverlayManager::updateTextOverlay(int id, const char* text)
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto* t = findText(id);
    if (!t) return false;
    return t->UpdateText(text);
}

bool OverlayManager::setTextOverlayRect(int id, const RECT& rect)
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto* t = findText(id);
    if (!t) return false;
    t->SetRect(rect);
    return true;
}

bool OverlayManager::setTextOverlayColor(int id, unsigned int color)
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto* t = findText(id);
    if (!t) return false;
    t->SetColor(color);
    return true;
}

// ── Image Overlay API ──

int OverlayManager::createImageOverlay(int width, int height, const void* bgraData, int stride)
{
    if (!d3dReady_ || !bgraData) {
        OVERLAY_LOG("createImageOverlay: d3dReady_=%d bgraData=%p", (int)d3dReady_, bgraData);
        return -1;
    }

    auto renderer = std::make_unique<ImageRenderer>(d3dDevice_, &shaders_);
    if (!renderer->Create(width, height, bgraData, stride)) {
        OVERLAY_LOG("createImageOverlay: ImageRenderer::Create failed (%dx%d)", width, height);
        return -1;
    }

    int id = nextImageId_++;
    renderer->SetId(id);

    std::lock_guard<std::mutex> lock(mutex_);
    images_.push_back(std::move(renderer));
    OVERLAY_LOG("createImageOverlay OK: id=%d %dx%d images_count=%zu", id, width, height, images_.size());
    return id;
}

void OverlayManager::destroyImageOverlay(int id)
{
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto it = images_.begin(); it != images_.end(); ++it) {
        if ((*it)->GetId() == id) {
            images_.erase(it);
            return;
        }
    }
}

bool OverlayManager::setImageOverlayRect(int id, const RECT& rect)
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto* img = findImage(id);
    if (!img) return false;
    img->SetRect(rect);
    return true;
}

bool OverlayManager::setImageOverlayColorTint(int id, unsigned int colorTint)
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto* img = findImage(id);
    if (!img) return false;
    img->SetColorTint(colorTint);
    return true;
}

// ── D3D 渲染 ──

void OverlayManager::drawD3D(ID3D11DeviceContext* ctx, int viewportW, int viewportH)
{
    if (!d3dReady_ || !ctx) return;

    // ── 显式设置 Alpha 混合状态（确保 overlay 背景透明）──
    float blendFactor[4] = { 0, 0, 0, 0 };
    if (blendState_) {
        ctx->OMSetBlendState(blendState_.Get(), blendFactor, 0xffffffff);
    }

    std::lock_guard<std::mutex> lock(mutex_);

    // 设置 overlay 共用采样器
    ID3D11SamplerState* samplers[] = { samplerState_.Get() };
    ctx->PSSetSamplers(0, 1, samplers);

    // 绘制文字 overlay
    for (auto& text : texts_) {
        text->Draw(ctx, viewportW, viewportH, dpiScale_);
    }

    // 绘制图片 overlay
    for (auto& image : images_) {
        image->Draw(ctx, viewportW, viewportH, dpiScale_);
    }
}

#ifdef ENABLE_WGL_INTEROP
// ── Qt 渲染 ──

void OverlayManager::drawQt(QPainter* painter, int viewportW, int viewportH)
{
    if (!painter) return;

    std::lock_guard<std::mutex> lock(mutex_);

    // 绘制文字 overlay
    for (auto& text : texts_) {
        const auto& rect = text->GetRect();
        unsigned int color = text->GetColor();
        int x = rect.left;
        int y = rect.top;
        int w = rect.right - rect.left;
        int h = rect.bottom - rect.top;
        if (w <= 0 || h <= 0) continue;

        // ARGB → QColor
        QColor qColor(
            (color >> 16) & 0xFF,  // R
            (color >> 8)  & 0xFF,  // G
            color & 0xFF,          // B
            (color >> 24) & 0xFF   // A
        );

        QFont font(QString::fromUtf8(text->GetFontName().c_str()), text->GetFontSize());
        painter->setFont(font);
        painter->setPen(qColor);
        painter->drawText(QRect(x, y, w, h), Qt::AlignLeft | Qt::AlignTop,
                          QString::fromUtf8(text->GetText().c_str()));
    }

    // 绘制图片 overlay
    for (auto& image : images_) {
        const auto& rect = image->GetRect();
        int x = rect.left;
        int y = rect.top;
        int w = rect.right - rect.left;
        int h = rect.bottom - rect.top;
        if (w <= 0 || h <= 0) continue;

        // 从 CPU 侧 BGRA 数据构造 QImage
        const auto& bgra = image->GetBgraData();
        int imgW = image->ImgWidth();
        int imgH = image->ImgHeight();
        if (bgra.empty() || imgW <= 0 || imgH <= 0) continue;

        QImage qimg(bgra.data(), imgW, imgH, imgW * 4,
                    QImage::Format_RGB32);  // Format_RGB32 = 0xffRRGGBB (matches BGRA in memory)
        painter->drawImage(QRect(x, y, w, h), qimg);
    }
}
#endif

// ── 查找 ──

TextRenderer* OverlayManager::findText(int id)
{
    for (auto& t : texts_) {
        if (t->GetId() == id) return t.get();
    }
    return nullptr;
}

ImageRenderer* OverlayManager::findImage(int id)
{
    for (auto& img : images_) {
        if (img->GetId() == id) return img.get();
    }
    return nullptr;
}

} // namespace muxplay
