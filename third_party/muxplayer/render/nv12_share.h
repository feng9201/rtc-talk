#pragma once
#include <d3d11.h>
#include <cstdint>
#include <string>

// ── NV12 纹理共享 + 最小 D3D11 NV12→RGBA 渲染 ──
// 替代 DxRender 在 GL 互通模式下的全部功能：
//   1. 从解码器 NV12 纹理 CopySubresourceRegion（同格式，保证拷贝成功）
//   2. D3D11 像素着色器 NV12→RGBA 渲染到共享 RGBA 纹理
//   3. GL 端通过 WGL_NV_DX_interop 注册共享 RGBA 纹理即可直接显示
//   （无 SwapChain、无窗口绑定、无 Clear+Present）

struct AVFrame;

namespace muxplay {

class Nv12TextureShare {
public:
    Nv12TextureShare() = default;
    ~Nv12TextureShare() { destroy(); }

    bool init(ID3D11Device* device, uint32_t width, uint32_t height);
    void destroy();
    bool resize(ID3D11Device* device, uint32_t width, uint32_t height);

    // 从解码器纹理拷贝 NV12 → 内部 NV12 → PS 渲染 → 共享 RGBA
    // ySubresource: frame->data[1]，解码器纹理数组中当前帧的 Y 平面索引
    void render(ID3D11DeviceContext* ctx, ID3D11Texture2D* decoderTex, int ySubresource);

    // 从 CPU 帧（AV_PIX_FMT_NV12 / YUV420P）上传到内部 NV12 纹理 → PS 渲染 → 共享 RGBA
    // 用于硬解降级到软解时的 fallback 路径。返回 false 表示不支持的格式。
    bool renderFromCPU(ID3D11DeviceContext* ctx, AVFrame* frame);

    ID3D11Texture2D* sharedTexture() const { return d3dSharedRGBA_; }
    uint32_t width()  const { return width_; }
    uint32_t height() const { return height_; }
    void setUUid(const std::string& uuid) { uuid_ = uuid; }

private:
    void releaseResources();
    bool compileShaders(ID3D11Device* device);
    bool createGeometry(ID3D11Device* device);
    // 内部 NV12 → PS 渲染 → 共享 RGBA（render / renderFromCPU 共用的渲染尾部）
    void renderNV12ToRGBA(ID3D11DeviceContext* ctx);

    // 内部 NV12 纹理（非共享，用于同格式拷贝）
    ID3D11Texture2D*            d3dNV12_   = nullptr;
    ID3D11ShaderResourceView*   d3dSRV_Y_  = nullptr;
    ID3D11ShaderResourceView*   d3dSRV_UV_ = nullptr;

    // Staging 纹理（CPU 上传用，Map/Unmap 方式避免 UpdateSubresource 对 planar 格式的限制）
    ID3D11Texture2D*            d3dNV12Staging_ = nullptr;

    // 共享 RGBA 渲染目标
    ID3D11Texture2D*            d3dSharedRGBA_ = nullptr;
    ID3D11RenderTargetView*     d3dSharedRTV_  = nullptr;

    // D3D11 渲染状态
    ID3D11VertexShader* d3dVS_       = nullptr;
    ID3D11PixelShader*  d3dPS_       = nullptr;
    ID3D11InputLayout*  d3dLayout_   = nullptr;
    ID3D11Buffer*       d3dVB_       = nullptr;
    ID3D11SamplerState* d3dSampler_  = nullptr;

    uint32_t width_  = 0;
    uint32_t height_ = 0;

    std::string uuid_;
};

} // namespace muxplay
