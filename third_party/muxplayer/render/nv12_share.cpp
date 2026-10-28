#include "nv12_share.h"
#include <d3dcompiler.h>
#include <vector>

extern "C" {
#include <libavutil/pixfmt.h>
#include <libavutil/frame.h>
}

namespace muxplay {

void Nv12TextureShare::releaseResources() {
    if (d3dSampler_)     { d3dSampler_->Release();    d3dSampler_ = nullptr; }
    if (d3dVB_)          { d3dVB_->Release();          d3dVB_ = nullptr; }
    if (d3dLayout_)      { d3dLayout_->Release();      d3dLayout_ = nullptr; }
    if (d3dPS_)          { d3dPS_->Release();          d3dPS_ = nullptr; }
    if (d3dVS_)          { d3dVS_->Release();          d3dVS_ = nullptr; }
    if (d3dSharedRTV_)   { d3dSharedRTV_->Release();   d3dSharedRTV_ = nullptr; }
    if (d3dSharedRGBA_)  { d3dSharedRGBA_->Release();  d3dSharedRGBA_ = nullptr; }
    if (d3dSRV_UV_)      { d3dSRV_UV_->Release();      d3dSRV_UV_ = nullptr; }
    if (d3dSRV_Y_)       { d3dSRV_Y_->Release();       d3dSRV_Y_ = nullptr; }    if (d3dNV12Staging_) { d3dNV12Staging_->Release(); d3dNV12Staging_ = nullptr; }    if (d3dNV12_)        { d3dNV12_->Release();        d3dNV12_ = nullptr; }
}

// ── 编译 D3D11 着色器 ──
bool Nv12TextureShare::compileShaders(ID3D11Device* device) {
    if (d3dPS_) return true; // 已编译

    // 顶点着色器
    const char* vsSrc = R"(
        struct VS_IN  { float3 p : POSITION; float2 t : TEXCOORD; };
        struct VS_OUT { float4 p : SV_POSITION; float2 t : TEXCOORD; };
        VS_OUT main(VS_IN v) { VS_OUT o; o.p = float4(v.p, 1); o.t = v.t; return o; }
    )";
    ID3DBlob* blob = nullptr;
    if (FAILED(D3DCompile(vsSrc, strlen(vsSrc), nullptr, nullptr, nullptr, "main", "vs_4_0", 0, 0, &blob, nullptr)))
        return false;
    device->CreateVertexShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &d3dVS_);
    if (!d3dVS_) { blob->Release(); return false; }

    // 输入布局
    D3D11_INPUT_ELEMENT_DESC layout[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},
        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0},
    };
    device->CreateInputLayout(layout, 2, blob->GetBufferPointer(), blob->GetBufferSize(), &d3dLayout_);
    blob->Release();
    if (!d3dLayout_) return false;

    // 像素着色器（BT.601 YUV→RGB）
    const char* psSrc = R"(
        Texture2D    yTex  : register(t0);
        Texture2D    uvTex : register(t1);
        SamplerState samp  : register(s0);
        float4 main(float4 p : SV_Position, float2 t : TEXCOORD) : SV_Target {
            float y = yTex.Sample(samp, t).r;
            float2 uv = uvTex.Sample(samp, t).rg - 0.5;
            float r = y + 1.402   * uv.g;
            float g = y - 0.344   * uv.r - 0.714 * uv.g;
            float b = y + 1.772   * uv.r;
            return float4(r, g, b, 1);
        }
    )";
    if (FAILED(D3DCompile(psSrc, strlen(psSrc), nullptr, nullptr, nullptr, "main", "ps_4_0", 0, 0, &blob, nullptr)))
        return false;
    device->CreatePixelShader(blob->GetBufferPointer(), blob->GetBufferSize(), nullptr, &d3dPS_);
    blob->Release();
    return d3dPS_ != nullptr;
}

// ── 全屏四边形顶点缓冲 ──
struct Vertex { float x, y, z; float u, v; };
bool Nv12TextureShare::createGeometry(ID3D11Device* device) {
    if (d3dVB_) return true;

    Vertex quad[] = {
        {-1,  1, 0, 0, 0},
        { 1,  1, 0, 1, 0},
        {-1, -1, 0, 0, 1},
        { 1, -1, 0, 1, 1},
    };
    D3D11_BUFFER_DESC desc = {};
    desc.ByteWidth = sizeof(quad);
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA init = { quad };
    if (FAILED(device->CreateBuffer(&desc, &init, &d3dVB_))) return false;

    // 采样器状态
    D3D11_SAMPLER_DESC samp = {};
    samp.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    samp.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    samp.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    samp.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    samp.MaxLOD = D3D11_FLOAT32_MAX;
    if (FAILED(device->CreateSamplerState(&samp, &d3dSampler_))) return false;

    return true;
}

// ── 初始化 ──
bool Nv12TextureShare::init(ID3D11Device* device, uint32_t width, uint32_t height) {
    if (!device || width == 0 || height == 0) return false;
    if (!compileShaders(device) || !createGeometry(device)) return false;
    return resize(device, width, height);
}

bool Nv12TextureShare::resize(ID3D11Device* device, uint32_t width, uint32_t height) {
    width_ = width;
    height_ = height;

    // 释放旧纹理
    if (d3dSRV_Y_)       { d3dSRV_Y_->Release();      d3dSRV_Y_ = nullptr; }
    if (d3dSRV_UV_)      { d3dSRV_UV_->Release();     d3dSRV_UV_ = nullptr; }
    if (d3dNV12Staging_) { d3dNV12Staging_->Release(); d3dNV12Staging_ = nullptr; }
    if (d3dNV12_)        { d3dNV12_->Release();       d3dNV12_ = nullptr; }
    if (d3dSharedRTV_)   { d3dSharedRTV_->Release();  d3dSharedRTV_ = nullptr; }
    if (d3dSharedRGBA_)  { d3dSharedRGBA_->Release(); d3dSharedRGBA_ = nullptr; }

    // 内部 NV12 纹理（非共享，同格式拷贝保证成功）
    D3D11_TEXTURE2D_DESC desc = {};
    desc.Width  = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_NV12;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    if (FAILED(device->CreateTexture2D(&desc, nullptr, &d3dNV12_))) return false;

    // Staging NV12 纹理（CPU 写入用，Map/Unmap 避免 UpdateSubresource 对 planar 格式的限制）
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    desc.MiscFlags = 0;
    device->CreateTexture2D(&desc, nullptr, &d3dNV12Staging_);

    // Y SRV（R8_UNORM 视图）
    D3D11_SHADER_RESOURCE_VIEW_DESC srvY = {};
    srvY.Format = DXGI_FORMAT_R8_UNORM;
    srvY.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvY.Texture2D.MipLevels = 1;
    device->CreateShaderResourceView(d3dNV12_, &srvY, &d3dSRV_Y_);

    // UV SRV（R8G8_UNORM 视图）
    D3D11_SHADER_RESOURCE_VIEW_DESC srvUV = {};
    srvUV.Format = DXGI_FORMAT_R8G8_UNORM;
    srvUV.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
    srvUV.Texture2D.MipLevels = 1;
    device->CreateShaderResourceView(d3dNV12_, &srvUV, &d3dSRV_UV_);

    // 共享 RGBA 渲染目标
    D3D11_TEXTURE2D_DESC sharedDesc = {};
    sharedDesc.Width  = width;
    sharedDesc.Height = height;
    sharedDesc.MipLevels = 1;
    sharedDesc.ArraySize = 1;
    sharedDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sharedDesc.SampleDesc.Count = 1;
    sharedDesc.Usage = D3D11_USAGE_DEFAULT;
    sharedDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    sharedDesc.MiscFlags = D3D11_RESOURCE_MISC_SHARED;
    if (FAILED(device->CreateTexture2D(&sharedDesc, nullptr, &d3dSharedRGBA_))) return false;
    if (FAILED(device->CreateRenderTargetView(d3dSharedRGBA_, nullptr, &d3dSharedRTV_))) return false;

    return true;
}

// ── 渲染：拷贝 → Draw → Flush ──
void Nv12TextureShare::render(ID3D11DeviceContext* ctx, ID3D11Texture2D* decoderTex, int ySubresource) {
    if (!ctx || !decoderTex || !d3dNV12_ || !d3dSharedRTV_) return;

    // 1. 拷贝 NV12 到内部纹理（同格式 DXGI_FORMAT_NV12 → DXGI_FORMAT_NV12，保证成功）
    ctx->CopySubresourceRegion(d3dNV12_, 0, 0, 0, 0, decoderTex, ySubresource, nullptr);       // Y 平面
    ctx->CopySubresourceRegion(d3dNV12_, 1, 0, 0, 0, decoderTex, ySubresource + 1, nullptr);   // UV 平面

    renderNV12ToRGBA(ctx);
}

// ── CPU 帧上传路径（硬解降级到软解时使用）──
// 使用 Map/Unmap staging 纹理方式，避免 UpdateSubresource 对 NV12 planar 格式的限制
bool Nv12TextureShare::renderFromCPU(ID3D11DeviceContext* ctx, AVFrame* frame) {
    if (!ctx || !frame || !d3dNV12_ || !d3dSharedRTV_ || !d3dNV12Staging_) return false;

    int w = frame->width;
    int h = frame->height;
    if (w <= 0 || h <= 0) return false;

    // 检查帧尺寸是否与纹理匹配
    if ((uint32_t)w != width_ || (uint32_t)h != height_) return false;

    // ── NV12 planar 格式：Map subresource 0 获取整个 Y+UV 数据 ──
    // D3D11 NV12 staging 纹理的 subresource 0 包含 Y 平面 + UV 平面，
    // UV 平面位于 Y 平面之后（偏移 = RowPitch * height，向上对齐到 256）。
    // 不能单独 Map subresource 1（返回 E_INVALIDARG）。
    D3D11_MAPPED_SUBRESOURCE mapped = {};
    HRESULT hr = ctx->Map(d3dNV12Staging_, 0, D3D11_MAP_WRITE, 0, &mapped);
    if (FAILED(hr)) return false;

    uint8_t* base = (uint8_t*)mapped.pData;
    int yPitch = mapped.RowPitch;

    // 拷贝 Y 平面
    const uint8_t* ySrc = frame->data[0];
    for (int row = 0; row < h; row++) {
        memcpy(base + row * yPitch, ySrc + row * frame->linesize[0], w);
    }

    // UV 平面偏移：Y 平面行数 * RowPitch（D3D11 已对齐）
    // 对于 NV12，UV 平面紧接 Y 平面，行数为 height/2
    uint8_t* uvBase = base + yPitch * h;
    int uvPitch = yPitch;  // UV 行间距与 Y 行间距相同
    int uvW = w / 2;
    int uvH = h / 2;

    if (frame->format == AV_PIX_FMT_NV12) {
        // NV12: UV 已交错，直接拷贝
        const uint8_t* uvSrc = frame->data[1];
        for (int row = 0; row < uvH; row++) {
            memcpy(uvBase + row * uvPitch, uvSrc + row * frame->linesize[1], uvW * 2);
        }
    } else if (frame->format == AV_PIX_FMT_YUV420P || frame->format == AV_PIX_FMT_YUVJ420P) {
        // YUV420P/YUVJ420P: U/V 分离，需交错 UV
        const uint8_t* uPlane = frame->data[1];
        const uint8_t* vPlane = frame->data[2];
        for (int row = 0; row < uvH; row++) {
            uint8_t* d = uvBase + row * uvPitch;
            const uint8_t* uSrc = uPlane + row * frame->linesize[1];
            const uint8_t* vSrc = vPlane + row * frame->linesize[2];
            for (int col = 0; col < uvW; col++) {
                d[col * 2]     = uSrc[col];
                d[col * 2 + 1] = vSrc[col];
            }
        }
    } else {
        ctx->Unmap(d3dNV12Staging_, 0);
        return false;
    }
    ctx->Unmap(d3dNV12Staging_, 0);

    // 从 staging 拷贝到 GPU 纹理（CopySubresourceRegion 对 DEFAULT 纹理的 planar 格式有效）
    ctx->CopySubresourceRegion(d3dNV12_, 0, 0, 0, 0, d3dNV12Staging_, 0, nullptr);

    renderNV12ToRGBA(ctx);
    return true;
}

// ── 内部 NV12 → PS 渲染 → 共享 RGBA（render / renderFromCPU 共用）──
void Nv12TextureShare::renderNV12ToRGBA(ID3D11DeviceContext* ctx) {
    // 设置渲染管线
    ctx->OMSetRenderTargets(1, &d3dSharedRTV_, nullptr);

    D3D11_VIEWPORT vp = { 0, 0, (float)width_, (float)height_, 0, 1 };
    ctx->RSSetViewports(1, &vp);

    ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
    ctx->IASetInputLayout(d3dLayout_);
    UINT stride = sizeof(Vertex);
    UINT offset = 0;
    ctx->IASetVertexBuffers(0, 1, &d3dVB_, &stride, &offset);

    ctx->VSSetShader(d3dVS_, nullptr, 0);
    ctx->PSSetShader(d3dPS_, nullptr, 0);
    ctx->PSSetSamplers(0, 1, &d3dSampler_);
    ctx->PSSetShaderResources(0, 1, &d3dSRV_Y_);
    ctx->PSSetShaderResources(1, 1, &d3dSRV_UV_);

    // 3. 绘制全屏四边形
    ctx->Draw(4, 0);

    // 4. Flush 确保 GPU 命令已提交（WGL lock 后续使用）
    ctx->Flush();
}

void Nv12TextureShare::destroy() {
    releaseResources();
    width_ = height_ = 0;
}

} // namespace muxplay
