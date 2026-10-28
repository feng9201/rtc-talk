#pragma once

#include "shader_manager.h"
#include <d3d11.h>
#include <wrl/client.h>
#include <vector>

namespace muxplay {

using Microsoft::WRL::ComPtr;

// ── 图片叠加渲染器 ──
// 将 BGRA 像素数据上传为 D3D11 IMMUTABLE 纹理，通过 ShaderManager 的
// "common" VS + "image" PS 进行 alpha 混合绘制。
class ImageRenderer {
public:
    ImageRenderer(ID3D11Device* device, ShaderManager* shaders);
    ~ImageRenderer();

    // 创建图片叠加（BGRA premultiplied alpha 像素数据 → D3D11 纹理）
    // stride: 每行字节数（0 或 width*4 表示连续数据）
    bool Create(int width, int height, const void* bgraData, int stride);

    // 释放 GPU 资源
    void Release();

    // D3D 渲染（在 render 线程调用）
    // dpiScale: 逻辑像素→物理像素的缩放比，用于高 DPI 适配
    bool Draw(ID3D11DeviceContext* ctx, int viewportW, int viewportH, float dpiScale = 1.0f);

    int  GetId() const { return id_; }
    void SetId(int id) { id_ = id; }
    int  Width() const { return imgWidth_; }
    int  Height() const { return imgHeight_; }
    bool IsValid() const { return texture_ != nullptr; }

    // ── 位置和色调（像素坐标，ARGB 格式，0xFFFFFFFF=原色）──
    void SetRect(const RECT& rect) { rect_ = rect; }
    void SetColorTint(unsigned int tint) { colorTint_ = tint; }
    const RECT& GetRect() const { return rect_; }
    unsigned int GetColorTint() const { return colorTint_; }

    // Qt 渲染所需的 CPU 侧像素数据
    const std::vector<uint8_t>& GetBgraData() const { return bgraData_; }
    int ImgWidth() const { return imgWidth_; }
    int ImgHeight() const { return imgHeight_; }

private:
    bool CreateDynamicVB();
    bool UpdateQuadVB(ID3D11DeviceContext* ctx, float x, float y, float w, float h,
                      int viewportW, int viewportH);

    ID3D11Device*  device_;
    ShaderManager* shaders_;

    int id_ = -1;
    int imgWidth_ = 0;
    int imgHeight_ = 0;

    // 存储的位置和色调
    RECT         rect_     = {0, 0, 0, 0};
    unsigned int colorTint_ = 0xFFFFFFFF;

    // CPU 侧像素数据（供 Qt 渲染路径使用）
    std::vector<uint8_t> bgraData_;

    // GPU 资源
    ComPtr<ID3D11Texture2D>          texture_;
    ComPtr<ID3D11ShaderResourceView> srv_;
    ComPtr<ID3D11Buffer>             constantBuffer_;
    ComPtr<ID3D11Buffer>             vertexBuffer_;
};

} // namespace muxplay
