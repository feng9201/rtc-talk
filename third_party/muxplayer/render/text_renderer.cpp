#include "text_renderer.h"
#include <cstring>
#include <string>
#include <cstdio>
#include <windows.h>

#define TEXT_LOG(fmt, ...) do { fprintf(stderr, "[TextRenderer] " fmt "\n", ##__VA_ARGS__); fflush(stderr); } while(0)

// ── Overlay 顶点结构（float4 pos + float2 uv，TRIANGLESTRIP 四边形）──
struct OverlayVertex {
    float x, y, z, w;   // NDC 坐标（z=0, w=1）
    float u, v;          // 纹理坐标
};

// 文字颜色常量缓冲区
struct TextConstants {
    float color[4]; // RGBA
};

// UTF-8 → wide string 转换
static std::wstring ToWide(const std::string& s) {
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    if (len <= 0) return std::wstring();
    std::wstring ws(len - 1, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &ws[0], len);
    return ws;
}

namespace muxplay {

TextRenderer::TextRenderer(ID3D11Device* device, ShaderManager* shaders, float dpiScale)
    : device_(device), shaders_(shaders), dpiScale_(dpiScale)
{
}

TextRenderer::~TextRenderer()
{
    Release();
}

bool TextRenderer::Create(const char* text, const char* fontName, int fontSize)
{
    if (!text || !fontName || fontSize <= 0) return false;
    if (!device_) return false;

    text_ = text;
    fontName_ = fontName;
    fontSize_ = fontSize;

    if (!RenderTextToTexture()) return false;
    if (!CreateDynamicVB()) return false;

    // 默认位置：文字自然尺寸在 (0,0)
    rect_ = {0, 0, textWidth_, textHeight_};

    return true;
}

void TextRenderer::Release()
{
    vertexBuffer_.Reset();
    constantBuffer_.Reset();
    srv_.Reset();
    texture_.Reset();
    textWidth_ = 0;
    textHeight_ = 0;
}

bool TextRenderer::UpdateText(const char* text)
{
    if (!text || text_ == text) return true;
    text_ = text;

    // 释放旧 GPU 资源并重建
    vertexBuffer_.Reset();
    srv_.Reset();
    texture_.Reset();

    if (!RenderTextToTexture()) return false;
    if (!CreateDynamicVB()) return false;

    // 更新默认位置为新文字的自然尺寸
    rect_ = {rect_.left, rect_.top, rect_.left + textWidth_, rect_.top + textHeight_};
    return true;
}

bool TextRenderer::RenderTextToTexture()
{
    // Step 1: GDI 渲染文字到内存 DC
    std::wstring wText = ToWide(text_);
    std::wstring wFont = ToWide(fontName_);

    HDC hdcScreen = GetDC(nullptr);
    if (!hdcScreen) return false;

    // ── 高 DPI 适配：按缩放比放大字体，使 GDI 纹理与物理像素 back buffer 对齐 ──
    int scaledFontSize = static_cast<int>(fontSize_ * dpiScale_ + 0.5f);

    // 创建字体
    LOGFONTW lf = {};
    lf.lfHeight = -scaledFontSize;
    lf.lfWeight = FW_NORMAL;
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfQuality = CLEARTYPE_QUALITY;
    wcsncpy_s(lf.lfFaceName, wFont.c_str(), LF_FACESIZE - 1);

    HFONT hFont = CreateFontIndirectW(&lf);
    if (!hFont) {
        ReleaseDC(nullptr, hdcScreen);
        return false;
    }

    HDC hdcMem = CreateCompatibleDC(hdcScreen);
    HFONT hOldFont = (HFONT)SelectObject(hdcMem, hFont);

    // 测量文字尺寸
    SIZE textSize;
    GetTextExtentPoint32W(hdcMem, wText.c_str(), static_cast<int>(wText.length()), &textSize);
    textWidth_ = textSize.cx;
    textHeight_ = textSize.cy;

    if (textWidth_ <= 0 || textHeight_ <= 0) {
        SelectObject(hdcMem, hOldFont);
        DeleteDC(hdcMem);
        DeleteObject(hFont);
        ReleaseDC(nullptr, hdcScreen);
        return false;
    }

    // 创建 DIB section
    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = textWidth_;
    bmi.bmiHeader.biHeight = -textHeight_; // top-down
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* pBits = nullptr;
    HBITMAP hBitmap = CreateDIBSection(hdcMem, &bmi, DIB_RGB_COLORS, &pBits, nullptr, 0);
    if (!hBitmap) {
        SelectObject(hdcMem, hOldFont);
        DeleteDC(hdcMem);
        DeleteObject(hFont);
        ReleaseDC(nullptr, hdcScreen);
        return false;
    }

    HBITMAP hOldBmp = (HBITMAP)SelectObject(hdcMem, hBitmap);

    // 渲染白色文字到黑色背景
    SetBkColor(hdcMem, RGB(0, 0, 0));
    SetBkMode(hdcMem, OPAQUE);
    SetTextColor(hdcMem, RGB(255, 255, 255));
    TextOutW(hdcMem, 0, 0, wText.c_str(), static_cast<int>(wText.length()));

    // Step 2: 创建 D3D11 纹理并上传位图数据
    D3D11_TEXTURE2D_DESC texDesc = {};
    texDesc.Width = textWidth_;
    texDesc.Height = textHeight_;
    texDesc.MipLevels = 1;
    texDesc.ArraySize = 1;
    texDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    texDesc.SampleDesc.Count = 1;
    texDesc.Usage = D3D11_USAGE_IMMUTABLE;
    texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA subData = {};
    subData.pSysMem = pBits;
    subData.SysMemPitch = textWidth_ * 4;

    HRESULT hr = device_->CreateTexture2D(&texDesc, &subData, texture_.GetAddressOf());

    // 清理 GDI 资源
    SelectObject(hdcMem, hOldBmp);
    SelectObject(hdcMem, hOldFont);
    DeleteObject(hBitmap);
    DeleteDC(hdcMem);
    DeleteObject(hFont);
    ReleaseDC(nullptr, hdcScreen);

    if (FAILED(hr)) {
        TEXT_LOG("CreateTexture2D failed hr=0x%08X", (unsigned)hr);
        return false;
    }

    // 创建 SRV
    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;
    hr = device_->CreateShaderResourceView(texture_.Get(), &srvDesc, srv_.GetAddressOf());
    if (FAILED(hr)) {
        TEXT_LOG("CreateShaderResourceView failed hr=0x%08X", (unsigned)hr);
        return false;
    }

    // 创建常量缓冲区（文字颜色）
    if (!constantBuffer_) {
        D3D11_BUFFER_DESC cbDesc = {};
        cbDesc.Usage = D3D11_USAGE_DYNAMIC;
        cbDesc.ByteWidth = sizeof(TextConstants);
        cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
        cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

        TextConstants cbData = {};
        cbData.color[0] = cbData.color[1] = cbData.color[2] = cbData.color[3] = 1.0f;

        D3D11_SUBRESOURCE_DATA initData = {};
        initData.pSysMem = &cbData;
        hr = device_->CreateBuffer(&cbDesc, &initData, constantBuffer_.GetAddressOf());
        if (FAILED(hr)) {
            TEXT_LOG("CreateConstantBuffer failed hr=0x%08X", (unsigned)hr);
            return false;
        }
    }

    return true;
}

bool TextRenderer::CreateDynamicVB()
{
    // 创建动态顶点缓冲区（4 顶点 TRIANGLESTRIP 四边形）
    D3D11_BUFFER_DESC vbDesc = {};
    vbDesc.Usage = D3D11_USAGE_DYNAMIC;
    vbDesc.ByteWidth = sizeof(OverlayVertex) * 4;
    vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    vbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    HRESULT hr = device_->CreateBuffer(&vbDesc, nullptr, vertexBuffer_.GetAddressOf());
    if (FAILED(hr)) {
        TEXT_LOG("CreateDynamicVB failed hr=0x%08X", (unsigned)hr);
        return false;
    }
    return true;
}

bool TextRenderer::UpdateQuadVB(ID3D11DeviceContext* ctx, float x, float y, float w, float h,
                                 int viewportW, int viewportH)
{
    if (!vertexBuffer_ || viewportW <= 0 || viewportH <= 0) return false;

    // 像素坐标 → NDC
    float left   = 2.0f * x / viewportW - 1.0f;
    float right  = 2.0f * (x + w) / viewportW - 1.0f;
    float top    = 1.0f - 2.0f * y / viewportH;
    float bottom = 1.0f - 2.0f * (y + h) / viewportH;

    OverlayVertex verts[4] = {
        { left,  top,    0, 1, 0, 0 },  // top-left
        { right, top,    0, 1, 1, 0 },  // top-right
        { left,  bottom, 0, 1, 0, 1 },  // bottom-left
        { right, bottom, 0, 1, 1, 1 },  // bottom-right
    };

    D3D11_MAPPED_SUBRESOURCE mapped;
    HRESULT hr = ctx->Map(vertexBuffer_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (FAILED(hr)) return false;
    memcpy(mapped.pData, verts, sizeof(verts));
    ctx->Unmap(vertexBuffer_.Get(), 0);

    return true;
}

bool TextRenderer::Draw(ID3D11DeviceContext* ctx, int viewportW, int viewportH, float dpiScale)
{
    if (!srv_ || !vertexBuffer_ || !ctx) return false;

    // ── 高 DPI 适配：逻辑像素 rect → 物理像素坐标 ──
    float x = static_cast<float>(rect_.left)   * dpiScale;
    float y = static_cast<float>(rect_.top)    * dpiScale;
    float w = static_cast<float>(rect_.right - rect_.left) * dpiScale;
    float h = static_cast<float>(rect_.bottom - rect_.top) * dpiScale;
    if (w <= 0 || h <= 0) return false;

    if (!UpdateQuadVB(ctx, x, y, w, h, viewportW, viewportH)) return false;

    // 更新颜色常量缓冲区
    D3D11_MAPPED_SUBRESOURCE mapped;
    HRESULT hr = ctx->Map(constantBuffer_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (SUCCEEDED(hr)) {
        auto* cb = static_cast<TextConstants*>(mapped.pData);
        // color_ 格式: 0xAARRGGBB
        cb->color[0] = ((color_ >> 16) & 0xFF) / 255.0f; // R
        cb->color[1] = ((color_ >> 8)  & 0xFF) / 255.0f; // G
        cb->color[2] = (color_ & 0xFF)         / 255.0f; // B
        cb->color[3] = ((color_ >> 24) & 0xFF) / 255.0f; // A
        ctx->Unmap(constantBuffer_.Get(), 0);
    }

    // 设置 shader 和资源
    ctx->IASetInputLayout(shaders_->GetInputLayout());
    ctx->VSSetShader(shaders_->GetVertexShader("common"), nullptr, 0);
    ctx->PSSetShader(shaders_->GetPixelShader("text"), nullptr, 0);

    ID3D11ShaderResourceView* srvs[] = { srv_.Get() };
    ctx->PSSetShaderResources(0, 1, srvs);
    ctx->PSSetConstantBuffers(0, 1, constantBuffer_.GetAddressOf());

    UINT stride = sizeof(OverlayVertex);
    UINT offset = 0;
    ctx->IASetVertexBuffers(0, 1, vertexBuffer_.GetAddressOf(), &stride, &offset);
    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    ctx->Draw(4, 0);

    // 清理 SRV 绑定
    ID3D11ShaderResourceView* nullSRVs[] = { nullptr };
    ctx->PSSetShaderResources(0, 1, nullSRVs);

    return true;
}

} // namespace muxplay
