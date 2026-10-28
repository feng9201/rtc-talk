#include "dx_render.h"
#include <d3d11.h>
#include <dxgi.h>
#include <d3dcompiler.h>
#include <iostream>
#include <vector>
#include <cstdint>
#include <DirectXMath.h>
#include <w_log_cpp.h>

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")



using namespace DirectX;

struct VertexPosTex1
{
	XMFLOAT3 pos;
	XMFLOAT2 tex;
	static const D3D11_INPUT_ELEMENT_DESC inputLayout[2];
};
const D3D11_INPUT_ELEMENT_DESC VertexPosTex1::inputLayout[2] = {
	{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
	{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 }
};

namespace muxplay
{
	DxRender::DxRender(HWND hwnd)
		:hwnd_(hwnd)
	{
	}
	DxRender::~DxRender()
	{
	}
	bool DxRender::InitRender(size_t width, size_t height, ID3D11Device* sharedDevice, ID3D11DeviceContext* sharedContext)
	{
        if (!sharedDevice || !sharedContext) return false;

        // ── P1: ComPtr 自动 AddRef，无需手动管理 ──
        d3dDevice_ = sharedDevice;
        d3dImmediateContext_ = sharedContext;

        HRESULT result = S_OK;
        // 获取 IDXGIDevice 接口
        result = d3dDevice_->QueryInterface(__uuidof(IDXGIDevice), (void**)d3dgiDevice_.GetAddressOf());
        if (result != S_OK) {
            return false;
        }
        // 获取 IDXGIAdapter 接口
        result = d3dgiDevice_->GetAdapter(d3dgiAdapter_.GetAddressOf());
        if (result != S_OK) {
            return false;
        }

        // 获取 IDXGIFactory 接口
        result = d3dgiAdapter_->GetParent(__uuidof(IDXGIFactory), (void**)d3dgiFactory_.GetAddressOf());
        if (result != S_OK) {
            return false;
        }

        // 创建交换链（GL 互通模式下不需要）
        if (!useGLInterop_) {
            DXGI_SWAP_CHAIN_DESC swapChainDesc = { 0 };
            swapChainDesc.BufferCount = 2;
            swapChainDesc.BufferDesc.Width = width;
            swapChainDesc.BufferDesc.Height = height;
            swapChainDesc.BufferDesc.RefreshRate.Numerator = 60;
            swapChainDesc.BufferDesc.RefreshRate.Denominator = 1;
            swapChainDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
            swapChainDesc.OutputWindow = hwnd_;
            swapChainDesc.SampleDesc.Count = 1;
            swapChainDesc.Windowed = TRUE;
            swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
            d3dgiFactory_->CreateSwapChain(d3dDevice_.Get(), &swapChainDesc, d3dSwapChain_.GetAddressOf());

            // ── P1: 禁止 DXGI 接管窗口消息（避免与 Qt 事件循环冲突）──
            d3dgiFactory_->MakeWindowAssociation(hwnd_, DXGI_MWA_NO_WINDOW_CHANGES);
        }

        // 记录初始交换链尺寸
        renderWidth_ = (uint32_t)width;
        renderHeight_ = (uint32_t)height;

        // ── Shader 编译（仅首次，缓存复用以避免重复编译）──
        if (!shadersReady_) {
            // 顶点着色器
            const char* vertexShaderCode =
                "struct VertexPosIn"
                "{"
                "    float3 PosL : POSITION;"
                "    float2 Tex : TEXCOORD;"
                "};"
                "struct VertexPosOut"
                "{"
                "	float4 PosH : SV_POSITION;"
                "	float2 Tex : TEXCOORD;"
                "};"
                "VertexPosOut main(VertexPosIn vIn)"
                "{"
                "	VertexPosOut vOut;"
                "	vOut.PosH=float4(vIn.PosL,1.0f);"
                "   vOut.Tex=vIn.Tex;"
                "	return vOut;"
                "}";

            Microsoft::WRL::ComPtr<ID3DBlob> vsBlob;
            D3DCompile(vertexShaderCode, strlen(vertexShaderCode), NULL, NULL, NULL, "main", "vs_4_0", 0, 0, vsBlob.GetAddressOf(), NULL);
            d3dDevice_->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), NULL, d3dVertexShader_.GetAddressOf());
            if (!d3dVertexShader_) return false;
            d3dDevice_->CreateInputLayout(VertexPosTex1::inputLayout, ARRAYSIZE(VertexPosTex1::inputLayout),
                vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), inputLayout_.GetAddressOf());

            // 像素着色器 lambda（编译带不同 YUV→RGB 矩阵的 PS）
            auto compilePS = [&](const char* matrix, Microsoft::WRL::ComPtr<ID3D11PixelShader>& outPS) {
                char code[640];
                snprintf(code, sizeof(code),
                    "Texture2D yTexture : register(t0);"
                    "Texture2D uvTexture : register(t1);"
                    "SamplerState samplerState : register(s0);"
                    "float4 main(float4 pos : SV_Position, float2 uv : TEXCOORD) : SV_Target"
                    "{"
                    "    float y = yTexture.Sample(samplerState, uv).r;"
                    "    float u = uvTexture.Sample(samplerState, uv).r-0.5f;"
                    "    float v = uvTexture.Sample(samplerState, uv).g-0.5f;"
                    "    float3 yuv = float3(y, u, v);"
                    "    float3 rgb = mul(float3x3(%s),yuv);"
                    "    return float4(saturate(rgb), 1.0);"
                    "}", matrix);
                Microsoft::WRL::ComPtr<ID3DBlob> blob;
                if (SUCCEEDED(D3DCompile(code, strlen(code), NULL, NULL, NULL, "main", "ps_4_0", 0, 0, blob.GetAddressOf(), NULL))) {
                    d3dDevice_->CreatePixelShader(blob->GetBufferPointer(), blob->GetBufferSize(), NULL, outPS.GetAddressOf());
                }
            };

            compilePS("1.0, 0.0, 1.402, 1.0, -0.344, -0.714, 1.0, 1.772, 0.0", d3dPixelShader_);
            compilePS("1.0, 0.0, 1.5748, 1.0, -0.1873, -0.4681, 1.0, 1.8556, 0.0", d3dPixelShaderBT709_);
            compilePS("1.0, 0.0, 1.4746, 1.0, -0.1646, -0.5714, 1.0, 1.8814, 0.0", d3dPixelShaderBT2020_);
            if (!d3dPixelShader_ || !d3dPixelShaderBT709_ || !d3dPixelShaderBT2020_) {
                return false;
            }

            // ── P1: 采样状态（CLAMP 模式，避免边缘镜像伪影）──
            D3D11_SAMPLER_DESC sampDesc;
            ZeroMemory(&sampDesc, sizeof(sampDesc));
            sampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
            sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
            sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
            sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
            sampDesc.ComparisonFunc = D3D11_COMPARISON_NEVER;
            sampDesc.MinLOD = 0;
            sampDesc.MaxLOD = D3D11_FLOAT32_MAX;
            d3dDevice_->CreateSamplerState(&sampDesc, samplerState_.GetAddressOf());

            // ── P0: 显式 BlendState（与 D3D11Render 一致：alpha 混合）──
            D3D11_BLEND_DESC blendDesc = {};
            blendDesc.RenderTarget[0].BlendEnable = TRUE;
            blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
            blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
            blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
            blendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
            blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
            blendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
            blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
            d3dDevice_->CreateBlendState(&blendDesc, blendState_.GetAddressOf());

            // ── P0: 显式 RasterizerState（与 D3D11Render 一致：不剔除）──
            D3D11_RASTERIZER_DESC rastDesc = {};
            rastDesc.FillMode = D3D11_FILL_SOLID;
            rastDesc.CullMode = D3D11_CULL_NONE;
            rastDesc.FrontCounterClockwise = FALSE;
            d3dDevice_->CreateRasterizerState(&rastDesc, rasterizerState_.GetAddressOf());

            // 四边形顶点缓冲区（坐标不变）
            VertexPosTex1 quadVertices[] = {
                { XMFLOAT3(-1.0f, 1.0f, 0.0f), XMFLOAT2(0.0f, 0.0f) },
                { XMFLOAT3(1.0f, 1.0f, 0.0f), XMFLOAT2(1.0f, 0.0f) },
                { XMFLOAT3(-1.0f, -1.0f, 0.0f), XMFLOAT2(0.0f, 1.0f) },
                { XMFLOAT3(1.0f, -1.0f, 0.0f), XMFLOAT2(1.0f, 1.0f) }
            };
            D3D11_BUFFER_DESC vertexBufferDesc = {};
            vertexBufferDesc.ByteWidth = sizeof(quadVertices);
            vertexBufferDesc.Usage = D3D11_USAGE_DEFAULT;
            vertexBufferDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
            D3D11_SUBRESOURCE_DATA vertexData = {};
            vertexData.pSysMem = quadVertices;
            d3dDevice_->CreateBuffer(&vertexBufferDesc, &vertexData, vertexBuffer_.GetAddressOf());

            shadersReady_ = true;
        }

        // 创建 NV12 纹理
        D3D11_TEXTURE2D_DESC textureDesc = {};
        textureDesc.Width = width;
        textureDesc.Height = height;
        textureDesc.MipLevels = 1;
        textureDesc.ArraySize = 1;
        textureDesc.Format = DXGI_FORMAT_NV12;
        textureDesc.SampleDesc.Count = 1;
        textureDesc.Usage = D3D11_USAGE_DEFAULT;        
        textureDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        d3dDevice_->CreateTexture2D(&textureDesc, nullptr, d3dTextureNV12_.GetAddressOf());

        D3D11_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
        srvDesc.Format = DXGI_FORMAT_R8_UNORM;
        srvDesc.ViewDimension = D3D11_SRV_DIMENSION_TEXTURE2D;
        srvDesc.Texture2D.MipLevels = textureDesc.MipLevels;
        d3dDevice_->CreateShaderResourceView(d3dTextureNV12_.Get(), &srvDesc, d3dShaderResourceViewY_.GetAddressOf());
        srvDesc.Format = DXGI_FORMAT_R8G8_UNORM;
        d3dDevice_->CreateShaderResourceView(d3dTextureNV12_.Get(), &srvDesc, d3dShaderResourceViewUV_.GetAddressOf());

        // 创建渲染目标视图（GL 模式下不需要 SwapChain RTV）
        if (!useGLInterop_) {
            Microsoft::WRL::ComPtr<ID3D11Texture2D> backBuffer;
            d3dSwapChain_->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)backBuffer.GetAddressOf());
            d3dDevice_->CreateRenderTargetView(backBuffer.Get(), NULL, d3dRenderTargetView_.GetAddressOf());
        }

        // ── 创建共享 RGBA 渲染目标纹理（WGL_NV_DX_interop 零拷贝共享）──
        {
            D3D11_TEXTURE2D_DESC sharedDesc = {};
            sharedDesc.Width = width;
            sharedDesc.Height = height;
            sharedDesc.MipLevels = 1;
            sharedDesc.ArraySize = 1;
            sharedDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
            sharedDesc.SampleDesc.Count = 1;
            sharedDesc.Usage = D3D11_USAGE_DEFAULT;
            sharedDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
            sharedDesc.MiscFlags = D3D11_RESOURCE_MISC_SHARED;
            if (FAILED(d3dDevice_->CreateTexture2D(&sharedDesc, nullptr, d3dSharedRGBA_.GetAddressOf()))) {
                d3dSharedRGBA_.Reset();
            } else {
                d3dDevice_->CreateRenderTargetView(d3dSharedRGBA_.Get(), nullptr, d3dSharedRTV_.GetAddressOf());
            }
        }

        // 设置渲染状态
        if (useGLInterop_ && d3dSharedRTV_) {
            d3dImmediateContext_->OMSetRenderTargets(1, d3dSharedRTV_.GetAddressOf(), NULL);
        } else {
            d3dImmediateContext_->OMSetRenderTargets(1, d3dRenderTargetView_.GetAddressOf(), NULL);
        }
        // 设置视口
        D3D11_VIEWPORT viewport = { 0, 0, (FLOAT)width, (FLOAT)height, 0, 1 };
        d3dImmediateContext_->RSSetViewports(1, &viewport);

        d3dImmediateContext_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
        d3dImmediateContext_->IASetInputLayout(inputLayout_.Get());
        UINT stride = sizeof(VertexPosTex1);
        UINT offset = 0;
        d3dImmediateContext_->IASetVertexBuffers(0, 1, vertexBuffer_.GetAddressOf(), &stride, &offset);
        d3dImmediateContext_->VSSetShader(d3dVertexShader_.Get(), NULL, 0);
        d3dImmediateContext_->PSSetShader(d3dPixelShader_.Get(), NULL, 0);
        d3dImmediateContext_->PSSetSamplers(0, 1, samplerState_.GetAddressOf());
        d3dImmediateContext_->PSSetShaderResources(0, 1, d3dShaderResourceViewY_.GetAddressOf());
        d3dImmediateContext_->PSSetShaderResources(1, 1, d3dShaderResourceViewUV_.GetAddressOf());

        // ── P0: 设置显式 BlendState + RasterizerState（消除驱动默认值不确定性）──
        float blendFactor[4] = { 0, 0, 0, 0 };
        d3dImmediateContext_->OMSetBlendState(blendState_.Get(), blendFactor, 0xffffffff);
        d3dImmediateContext_->RSSetState(rasterizerState_.Get());

        // ── PIP 画中画：创建动态顶点缓冲区 ──
        {
            D3D11_BUFFER_DESC vbDesc = {};
            vbDesc.Usage = D3D11_USAGE_DYNAMIC;
            vbDesc.ByteWidth = sizeof(VertexPosTex1) * 4;
            vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
            vbDesc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
            d3dDevice_->CreateBuffer(&vbDesc, nullptr, pipVB_.GetAddressOf());
        }

        return true;
	}
	// ── 防闪烁：由 D3DRenderWidget 在 WM_SIZE 时主动调用 ──
	void DxRender::ResizeToRect(uint32_t w, uint32_t h)
	{
		if (w == 0 || h == 0) return;
		if (w == renderWidth_ && h == renderHeight_) return;
		if (!d3dSwapChain_) return;  // GL 模式无 SwapChain

		renderWidth_ = w;
		renderHeight_ = h;

		// 解绑 RTV → ResizeBuffers → 重建 RTV
		d3dRenderTargetView_.Reset();
		HRESULT hr = d3dSwapChain_->ResizeBuffers(0, w, h, DXGI_FORMAT_UNKNOWN, 0);
		if (SUCCEEDED(hr)) {
			Microsoft::WRL::ComPtr<ID3D11Texture2D> backBuffer;
			if (SUCCEEDED(d3dSwapChain_->GetBuffer(0, __uuidof(ID3D11Texture2D),
				(void**)backBuffer.GetAddressOf()))) {
				d3dDevice_->CreateRenderTargetView(backBuffer.Get(), NULL,
					d3dRenderTargetView_.GetAddressOf());
			}
			d3dImmediateContext_->OMSetRenderTargets(1,
				d3dRenderTargetView_.GetAddressOf(), NULL);
		}

		// 更新视口
		D3D11_VIEWPORT vp = { 0, 0, (float)w, (float)h, 0, 1 };
		d3dImmediateContext_->RSSetViewports(1, &vp);

		// 立即清屏并 present，避免 DWM 合成空帧闪烁
		float black[4] = { 0, 0, 0, 1 };
		d3dImmediateContext_->ClearRenderTargetView(d3dRenderTargetView_.Get(), black);
		d3dSwapChain_->Present(0, 0);  // 立即提交，不等 VSync
	}

	bool DxRender::renderVideo(AVFrame* frame)
	{
		if (!frame || !frame->data[0]) {
            LOG_WARN("[DxRender][uuid={}] invalid frame", uuid_);
            return false;
        }

		// ── P0: 零拷贝路径：GPU→GPU 拷贝 Y + UV 双平面 ──
        if (frame->format == AV_PIX_FMT_D3D11) {
            ID3D11Texture2D* decoderTexture = (ID3D11Texture2D*)(intptr_t)frame->data[0];
            int ySubresource = (int)(intptr_t)frame->data[1];
            d3dImmediateContext_->CopySubresourceRegion(d3dTextureNV12_.Get(), 0, 0, 0, 0, decoderTexture, ySubresource, nullptr);
            d3dImmediateContext_->CopySubresourceRegion(d3dTextureNV12_.Get(), 1, 0, 0, 0, decoderTexture, ySubresource + 1, nullptr);
        }
        // ── CPU 帧上传路径：YUV420P → NV12 纹理 ──
        else if (frame->format == AV_PIX_FMT_YUV420P || frame->format == AV_PIX_FMT_YUVJ420P) {
            int w = frame->width;
            int h = frame->height;
            if (w > 0 && h > 0 && d3dTextureNV12_) {
                // Y 平面: subresource 0 (R8_UNORM, w x h)
                D3D11_BOX yBox = { 0, 0, 0, (UINT)w, (UINT)h, 1 };
                d3dImmediateContext_->UpdateSubresource(
                    d3dTextureNV12_.Get(), 0, &yBox,
                    frame->data[0], frame->linesize[0], 0);

                // UV 平面: subresource 1 (R8G8_UNORM, w/2 x h/2)
                // YUV420P 的 U/V 是独立平面，NV12 需要交错 UV
                int uvW = w / 2;
                int uvH = h / 2;
                if (uvW > 0 && uvH > 0) {
                    std::vector<uint8_t> uvBuf(uvW * 2 * uvH);
                    const uint8_t* uPlane = frame->data[1];
                    const uint8_t* vPlane = frame->data[2];
                    for (int row = 0; row < uvH; row++) {
                        uint8_t* dst = uvBuf.data() + row * uvW * 2;
                        const uint8_t* uSrc = uPlane + row * frame->linesize[1];
                        const uint8_t* vSrc = vPlane + row * frame->linesize[2];
                        for (int col = 0; col < uvW; col++) {
                            dst[col * 2]     = uSrc[col];
                            dst[col * 2 + 1] = vSrc[col];
                        }
                    }
                    D3D11_BOX uvBox = { 0, 0, 0, (UINT)uvW, (UINT)uvH, 1 };
                    d3dImmediateContext_->UpdateSubresource(
                        d3dTextureNV12_.Get(), 1, &uvBox,
                        uvBuf.data(), uvW * 2, 0);
                }
            }
        }

        // 根据帧色彩空间选择 YUV→RGB 矩阵
        ID3D11PixelShader* selectedPS = d3dPixelShader_.Get();
        switch (frame->colorspace) {
            case AVCOL_SPC_BT709:
                selectedPS = d3dPixelShaderBT709_.Get();
                break;
            case AVCOL_SPC_BT2020_NCL:
            case AVCOL_SPC_BT2020_CL:
                selectedPS = d3dPixelShaderBT2020_.Get();
                break;
            default:
                break;
        }
        // ── 恢复完整视频渲染管线状态（drawD3D overlay 可能修改了 VS/IL/VB/SRV/Blend）──
        d3dImmediateContext_->IASetInputLayout(inputLayout_.Get());
        d3dImmediateContext_->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);
        UINT stride = sizeof(VertexPosTex1);
        UINT offset = 0;
        d3dImmediateContext_->IASetVertexBuffers(0, 1, vertexBuffer_.GetAddressOf(), &stride, &offset);
        d3dImmediateContext_->VSSetShader(d3dVertexShader_.Get(), NULL, 0);
        d3dImmediateContext_->PSSetShader(selectedPS, NULL, 0);
        d3dImmediateContext_->PSSetShaderResources(0, 1, d3dShaderResourceViewY_.GetAddressOf());
        d3dImmediateContext_->PSSetShaderResources(1, 1, d3dShaderResourceViewUV_.GetAddressOf());
        d3dImmediateContext_->PSSetSamplers(0, 1, samplerState_.GetAddressOf());
        float blendFactor[4] = { 0, 0, 0, 0 };
        d3dImmediateContext_->OMSetBlendState(blendState_.Get(), blendFactor, 0xffffffff);

        float clearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
        if (useGLInterop_ && d3dSharedRTV_) {
            d3dImmediateContext_->OMSetRenderTargets(1, d3dSharedRTV_.GetAddressOf(), NULL);
            d3dImmediateContext_->ClearRenderTargetView(d3dSharedRTV_.Get(), clearColor);
            d3dImmediateContext_->Draw(4, 0);
            d3dImmediateContext_->Flush();
        } else {
            d3dImmediateContext_->ClearRenderTargetView(d3dRenderTargetView_.Get(), clearColor);
            d3dImmediateContext_->Draw(4, 0);
        }

        // ── PIP 画中画：在指定位置二次绘制同一帧 NV12 纹理 ──
        if (pipEnabled_ && pipVB_ && pipRect_.right > pipRect_.left && pipRect_.bottom > pipRect_.top) {
            // 逻辑像素 → 物理像素
            float px = static_cast<float>(pipRect_.left)   * dpiScale_;
            float py = static_cast<float>(pipRect_.top)    * dpiScale_;
            float pw = static_cast<float>(pipRect_.right - pipRect_.left) * dpiScale_;
            float ph = static_cast<float>(pipRect_.bottom - pipRect_.top) * dpiScale_;
            float vpW = static_cast<float>(renderWidth_);
            float vpH = static_cast<float>(renderHeight_);
            if (vpW > 0 && vpH > 0 && pw > 0 && ph > 0) {
                // 像素坐标 → NDC
                float left   = 2.0f * px / vpW - 1.0f;
                float right  = 2.0f * (px + pw) / vpW - 1.0f;
                float top    = 1.0f - 2.0f * py / vpH;
                float bottom = 1.0f - 2.0f * (py + ph) / vpH;

                VertexPosTex1 pipVerts[4] = {
                    { XMFLOAT3(left,  top,    0.0f), XMFLOAT2(0.0f, 0.0f) },
                    { XMFLOAT3(right, top,    0.0f), XMFLOAT2(1.0f, 0.0f) },
                    { XMFLOAT3(left,  bottom, 0.0f), XMFLOAT2(0.0f, 1.0f) },
                    { XMFLOAT3(right, bottom, 0.0f), XMFLOAT2(1.0f, 1.0f) },
                };
                D3D11_MAPPED_SUBRESOURCE mapped;
                if (SUCCEEDED(d3dImmediateContext_->Map(pipVB_.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
                    memcpy(mapped.pData, pipVerts, sizeof(pipVerts));
                    d3dImmediateContext_->Unmap(pipVB_.Get(), 0);
                }
                UINT stride = sizeof(VertexPosTex1);
                UINT offset = 0;
                d3dImmediateContext_->IASetVertexBuffers(0, 1, pipVB_.GetAddressOf(), &stride, &offset);
                d3dImmediateContext_->Draw(4, 0);
            }
        }

		return true;
	}

	void DxRender::present()
	{
		if (!useGLInterop_ && d3dSwapChain_) {
			HRESULT hr = d3dSwapChain_->Present(1, 0);
			// ── P1: Device Lost 检测 ──
			if (hr == DXGI_ERROR_DEVICE_REMOVED || hr == DXGI_ERROR_DEVICE_RESET) {
				deviceLost_ = true;
			}
		}
	}

	// ── P1: Device Lost 恢复（重建 RTV）──
	bool DxRender::HandleDeviceLost()
	{
		if (!deviceLost_) return true;

		d3dRenderTargetView_.Reset();

		HRESULT hr = d3dSwapChain_->ResizeBuffers(0, renderWidth_, renderHeight_, DXGI_FORMAT_UNKNOWN, 0);
		if (FAILED(hr)) return false;

		Microsoft::WRL::ComPtr<ID3D11Texture2D> backBuffer;
		hr = d3dSwapChain_->GetBuffer(0, __uuidof(ID3D11Texture2D), (void**)backBuffer.GetAddressOf());
		if (FAILED(hr)) return false;

		hr = d3dDevice_->CreateRenderTargetView(backBuffer.Get(), NULL, d3dRenderTargetView_.GetAddressOf());
		if (FAILED(hr)) return false;

		deviceLost_ = false;
		return true;
	}

	void DxRender::destroy()
	{
        LOG_INFO("[DxRender][uuid={}] D3D渲染窗口 destroy", uuid_);
		// 1. 必须先退出全屏模式
		if (d3dSwapChain_) {
			d3dSwapChain_->SetFullscreenState(FALSE, NULL);
		}
		// 通知 DXGI 停止监听该窗口的 ALT+ENTER 等消息
		if (d3dgiFactory_ && hwnd_) {
			d3dgiFactory_->MakeWindowAssociation(hwnd_, DXGI_MWA_NO_WINDOW_CHANGES);
		}

		// 2. 清除设备上下文中的所有绑定状态
		if (d3dImmediateContext_) {
			d3dImmediateContext_->ClearState();
			d3dImmediateContext_->Flush();
		}

		// 3. P1: ComPtr::Reset() 自动 Release + 置空，无需手动逐行释放
		d3dRenderTargetView_.Reset();
		d3dSharedRTV_.Reset();
		d3dShaderResourceViewUV_.Reset();
		d3dShaderResourceViewY_.Reset();
		d3dTextureNV12_.Reset();
		d3dSharedRGBA_.Reset();
		samplerState_.Reset();
		blendState_.Reset();
		rasterizerState_.Reset();
		pipVB_.Reset();
		inputLayout_.Reset();
		vertexBuffer_.Reset();
		d3dPixelShader_.Reset();
		d3dPixelShaderBT709_.Reset();
		d3dPixelShaderBT2020_.Reset();
		d3dBlob_.Reset();
		d3dVertexShader_.Reset();
		d3dSwapChain_.Reset();
		d3dgiFactory_.Reset();
		d3dgiAdapter_.Reset();
		d3dgiDevice_.Reset();
		// ImmediateContext 必须在 Device 之前释放
		d3dImmediateContext_.Reset();
		d3dDevice_.Reset();

		// 4. 清除窗口句柄引用
		hwnd_ = nullptr;
    }

}