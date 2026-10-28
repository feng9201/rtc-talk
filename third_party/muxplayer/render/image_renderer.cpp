#include "image_renderer.h"
#include <cstring>
#include <cstdio>

#define IMAGE_LOG(fmt, ...) do { fprintf(stderr, "[ImageRenderer] " fmt "\n", ##__VA_ARGS__); fflush(stderr); } while(0)

// ── Overlay 顶点结构（与 TextRenderer 一致）──
struct OverlayVertexImg {
    float x, y, z, w;
    float u, v;
};

// 图片色调常量缓冲区
struct ImageConstants {
    float color[4]; // RGBA tint multiplier
};

namespace muxplay {

ImageRenderer::ImageRenderer(ID3D11Device* device, ShaderManager* shaders)
    : device_(device), shaders_(shaders)
{
}

ImageRenderer::~ImageRenderer()
{
    Release();
}

bool ImageRenderer::Create(int width, int height, const void* bgraData, int stride)
{
    if (!device_ || !bgraData || width <= 0 || height <= 0)
        return false;

    Release();
    imgWidth_ = width;
    imgHeight_ = height;

    // 保存 CPU 侧像素数据（供 Qt 渲染路径使用）
    int dataStride = stride > 0 ? stride : width * 4;
    bgraData_.resize(dataStride * height);
    if (dataStride == width * 4) {
        memcpy(bgraData_.data(), bgraData, bgraData_.size());
    } else {
        // 非 contiguous stride，逐行拷贝
        for (int row = 0; row < height; ++row) {
            memcpy(bgraData_.data() + row * width * 4,
                   static_cast<const uint8_t*>(bgraData) + row * dataStride,
                   width * 4);
        }
    }

    // 创建 IMMUTABLE 纹理
    D3D11_TEXTURE2D_DESC td = {};
    td.Width = width;
    td.Height = height;
    td.MipLevels = 1;
    td.ArraySize = 1;
    td.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_IMMUTABLE;
    td.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA initData = {};
    initData.pSysMem = bgraData;
    initData.SysMemPitch = dataStride;

    HRESULT hr = device_->CreateTexture2D(&td, &initData, texture_.GetAddressOf());
    if (FAILED(hr)) {
        IMAGE_LOG("CreateTexture2D failed (%dx%d) hr=0x%08X", width, height, (unsigned)hr);
        return false;
    }

    // 创建 SRV
    D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
    srvDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvDesc.Texture2D.MipLevels = 1;
    hr = device_->CreateShaderResourceView(texture_.Get(), &srvDesc, srv_.GetAddressOf());
    if (FAILED(hr)) {
        IMAGE_LOG("CreateShaderResourceView failed hr=0x%08X", (unsigned)hr);
        return false;
    }

    // 创建常量缓冲区（色调）
    D3D11_BUFFER_DESC cbDesc = {};
    cbDesc.ByteWidth = sizeof(ImageConstants);
    cbDesc.Usage = D3D11_USAGE_DYNAMIC;
    cbDesc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    cbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    ImageConstants cbData = {};
    cbData.color[0] = cbData.color[1] = cbData.color[2] = cbData.color[3] = 1.0f;
    D3D11_SUBRESOURCE_DATA cbInit = {};
    cbInit.pSysMem = &cbData;
    hr = device_->CreateBuffer(&cbDesc, &cbInit, constantBuffer_.GetAddressOf());
    if (FAILED(hr)) return false;

    // 创建动态顶点缓冲区
    if (!CreateDynamicVB()) return false;

    // 默认位置：图片原始尺寸在 (0,0)
    rect_ = {0, 0, width, height};

    return true;
}

void ImageRenderer::Release()
{
    vertexBuffer_.Reset();
    constantBuffer_.Reset();
    srv_.Reset();
    texture_.Reset();
    bgraData_.clear();
    imgWidth_ = 0;
    imgHeight_ = 0;
}

bool ImageRenderer::CreateDynamicVB()
{
    D3D11_BUFFER_DESC vbDesc = {};
    vbDesc.Usage = D3D11_USAGE_DYNAMIC;
    vbDesc.ByteWidth = sizeof(OverlayVertexImg) * 4;
    vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    vbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

    HRESULT hr = device_->CreateBuffer(&vbDesc, nullptr, vertexBuffer_.GetAddressOf());
    if (FAILED(hr)) {
        IMAGE_LOG("CreateDynamicVB failed hr=0x%08X", (unsigned)hr);
        return false;
    }
    return true;
}

bool ImageRenderer::UpdateQuadVB(ID3D11DeviceContext* ctx, float x, float y, float w, float h,
                                  int viewportW, int viewportH)
{
    if (!vertexBuffer_ || viewportW <= 0 || viewportH <= 0)
        return false;

    float left   = 2.0f * x / viewportW - 1.0f;
    float right  = 2.0f * (x + w) / viewportW - 1.0f;
    float top    = 1.0f - 2.0f * y / viewportH;
    float bottom = 1.0f - 2.0f * (y + h) / viewportH;

    OverlayVertexImg verts[4] = {
        { left,  top,    0, 1, 0, 0 },
        { right, top,    0, 1, 1, 0 },
        { left,  bottom, 0, 1, 0, 1 },
        { right, bottom, 0, 1, 1, 1 },
    };

    D3D11_MAPPED_SUBRESOURCE mapped;
    HRESULT hr = ctx->Map(vertexBuffer_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (FAILED(hr)) return false;
    memcpy(mapped.pData, verts, sizeof(verts));
    ctx->Unmap(vertexBuffer_.Get(), 0);

    return true;
}

bool ImageRenderer::Draw(ID3D11DeviceContext* ctx, int viewportW, int viewportH, float dpiScale)
{
    if (!srv_ || !vertexBuffer_ || !ctx)
        return false;

    // ── 高 DPI 适配：逻辑像素 rect → 物理像素坐标 ──
    float x = static_cast<float>(rect_.left)   * dpiScale;
    float y = static_cast<float>(rect_.top)    * dpiScale;
    float w = static_cast<float>(rect_.right - rect_.left) * dpiScale;
    float h = static_cast<float>(rect_.bottom - rect_.top) * dpiScale;
    if (w <= 0 || h <= 0)
        return false;

    if (!UpdateQuadVB(ctx, x, y, w, h, viewportW, viewportH)) return false;

    // 更新色调常量缓冲区
    D3D11_MAPPED_SUBRESOURCE mapped;
    HRESULT hr = ctx->Map(constantBuffer_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    if (SUCCEEDED(hr)) {
        auto* cb = static_cast<ImageConstants*>(mapped.pData);
        // colorTint_ 格式: 0xAARRGGBB
        cb->color[0] = ((colorTint_ >> 16) & 0xFF) / 255.0f; // R
        cb->color[1] = ((colorTint_ >> 8)  & 0xFF) / 255.0f; // G
        cb->color[2] = (colorTint_ & 0xFF)         / 255.0f; // B
        cb->color[3] = ((colorTint_ >> 24) & 0xFF) / 255.0f; // A
        ctx->Unmap(constantBuffer_.Get(), 0);
    }

    // 设置 shader 和资源
    ctx->IASetInputLayout(shaders_->GetInputLayout());
    ctx->VSSetShader(shaders_->GetVertexShader("common"), nullptr, 0);
    ctx->PSSetShader(shaders_->GetPixelShader("image"), nullptr, 0);

    ID3D11ShaderResourceView* srvs[] = { srv_.Get() };
    ctx->PSSetShaderResources(0, 1, srvs);
    ctx->PSSetConstantBuffers(0, 1, constantBuffer_.GetAddressOf());

    UINT stride = sizeof(OverlayVertexImg);
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
