#pragma once

#include "shader_manager.h"
#include <d3d11.h>
#include <wrl/client.h>
#include <string>

namespace muxplay {

using Microsoft::WRL::ComPtr;

// ── 文字叠加渲染器 ──
// 使用 GDI 渲染文字到 D3D11 BGRA 纹理，通过 ShaderManager 的
// "common" VS + "text" PS 进行 alpha 混合绘制。
class TextRenderer {
public:
    TextRenderer(ID3D11Device* device, ShaderManager* shaders, float dpiScale = 1.0f);
    ~TextRenderer();

    // 创建文字叠加（GDI→D3D11 纹理）
    bool Create(const char* text, const char* fontName, int fontSize);
    // 更新文字内容（重建 GDI 纹理）
    bool UpdateText(const char* text);
    // 释放 GPU 资源
    void Release();

    // D3D 渲染（在 render 线程调用）
    // dpiScale: 逻辑像素→物理像素的缩放比，用于高 DPI 适配
    bool Draw(ID3D11DeviceContext* ctx, int viewportW, int viewportH, float dpiScale = 1.0f);

    int  GetId() const { return id_; }
    void SetId(int id) { id_ = id; }
    void GetTextSize(int& w, int& h) const { w = textWidth_; h = textHeight_; }

    // ── 位置和颜色（像素坐标，ARGB 格式）──
    void SetRect(const RECT& rect) { rect_ = rect; }
    void SetColor(unsigned int color) { color_ = color; }
    const RECT& GetRect() const { return rect_; }
    unsigned int GetColor() const { return color_; }

    const std::string& GetText() const { return text_; }
    const std::string& GetFontName() const { return fontName_; }
    int GetFontSize() const { return fontSize_; }

private:
    bool RenderTextToTexture();
    bool CreateDynamicVB();
    bool UpdateQuadVB(ID3D11DeviceContext* ctx, float x, float y, float w, float h,
                      int viewportW, int viewportH);

    ID3D11Device*  device_;
    ShaderManager* shaders_;

    std::string text_;
    std::string fontName_;
    int fontSize_ = 16;
    float dpiScale_ = 1.0f;   // 高 DPI 缩放比（逻辑→物理像素）
    int id_ = -1;
    int textWidth_ = 0;
    int textHeight_ = 0;

    // 存储的位置和颜色
    RECT         rect_  = {0, 0, 0, 0};  // 默认在 Create 时按文字自然尺寸设置
    unsigned int color_ = 0xFFFFFFFF;     // 白色不透明

    // GPU 资源
    ComPtr<ID3D11Texture2D>          texture_;
    ComPtr<ID3D11ShaderResourceView> srv_;
    ComPtr<ID3D11Buffer>             constantBuffer_;
    ComPtr<ID3D11Buffer>             vertexBuffer_;
};

} // namespace muxplay
