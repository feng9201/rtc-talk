#include "shader_manager.h"
#include <d3d11.h>
#include <d3dcompiler.h>
#include <cstdio>
#include <cstring>

#pragma comment(lib, "d3dcompiler.lib")

#define SHADER_LOG(fmt, ...) do { fprintf(stderr, "[ShaderManager] " fmt "\n", ##__VA_ARGS__); fflush(stderr); } while(0)

// ═══════════════════════════════════════════════════════════════
//  Inline HLSL shader sources
// ═══════════════════════════════════════════════════════════════

// ── Overlay 通用顶点着色器（float4 pos + float2 uv）──
static const char* g_vsCommon = R"(
struct VS_INPUT {
    float4 pos : POSITION;
    float2 uv  : TEXCOORD0;
};
struct VS_OUTPUT {
    float4 pos : SV_POSITION;
    float2 uv  : TEXCOORD0;
};
VS_OUTPUT main(VS_INPUT input) {
    VS_OUTPUT output;
    output.pos = input.pos;
    output.uv  = input.uv;
    return output;
}
)";

// ── 文字叠加像素着色器 ──
// 纹理存储白色文字 + 黑色背景，R 通道作为 alpha 掩码
static const char* g_psText = R"(
Texture2D texText : register(t0);
SamplerState samp : register(s0);
cbuffer TextCB : register(b0) {
    float4 textColor;
};
struct PS_INPUT {
    float4 pos : SV_POSITION;
    float2 uv  : TEXCOORD0;
};
float4 main(PS_INPUT input) : SV_TARGET {
    float alpha = texText.Sample(samp, input.uv).r;
    return float4(textColor.rgb, textColor.a * alpha);
}
)";

// ── 图片叠加像素着色器 ──
// BGRA 纹理 + 颜色色调乘数（支持 premultiplied alpha）
static const char* g_psImage = R"(
Texture2D texImage : register(t0);
SamplerState samp : register(s0);
cbuffer ImageCB : register(b0) {
    float4 tintColor;
};
struct PS_INPUT {
    float4 pos : SV_POSITION;
    float2 uv  : TEXCOORD0;
};
float4 main(PS_INPUT input) : SV_TARGET {
    float4 color = texImage.Sample(samp, input.uv);
    return float4(color.rgb * tintColor.rgb, color.a * tintColor.a);
}
)";

// ── NV12 像素着色器（TV range → full range，BT.601）──
static const char* g_psNv12 = R"(
Texture2D texY  : register(t0);
Texture2D texUV : register(t1);
SamplerState samp : register(s0);
struct PS_INPUT {
    float4 pos : SV_POSITION;
    float2 uv  : TEXCOORD0;
};
float4 main(PS_INPUT input) : SV_TARGET {
    float  y = texY.Sample(samp, input.uv).r;
    float2 uv = texUV.Sample(samp, input.uv).rg;
    y      = saturate((y * 255.0 - 16.0) / 219.0);
    float u = (uv.x * 255.0 - 16.0) / 224.0 - 0.5;
    float v = (uv.y * 255.0 - 16.0) / 224.0 - 0.5;
    float r = y + 1.402   * v;
    float g = y - 0.34414 * u - 0.71414 * v;
    float b = y + 1.772   * u;
    return float4(saturate(r), saturate(g), saturate(b), 1.0);
}
)";

// ── YUV420P 像素着色器（TV range → full range，BT.601）──
static const char* g_psYuv420 = R"(
Texture2D texY : register(t0);
Texture2D texU : register(t1);
Texture2D texV : register(t2);
SamplerState samp : register(s0);
struct PS_INPUT {
    float4 pos : SV_POSITION;
    float2 uv  : TEXCOORD0;
};
float4 main(PS_INPUT input) : SV_TARGET {
    float y = texY.Sample(samp, input.uv).r;
    float u = texU.Sample(samp, input.uv).r;
    float v = texV.Sample(samp, input.uv).r;
    y = saturate((y * 255.0 - 16.0) / 219.0);
    u = (u * 255.0 - 16.0) / 224.0 - 0.5;
    v = (v * 255.0 - 16.0) / 224.0 - 0.5;
    float r = y + 1.402   * v;
    float g = y - 0.34414 * u - 0.71414 * v;
    float b = y + 1.772   * u;
    return float4(saturate(r), saturate(g), saturate(b), 1.0);
}
)";

// ── RGB 像素着色器 ──
static const char* g_psRgb = R"(
Texture2D texRGB : register(t0);
SamplerState samp : register(s0);
cbuffer FormatCB : register(b0) {
    int swapRB;
    int hasAlpha;
};
struct PS_INPUT {
    float4 pos : SV_POSITION;
    float2 uv  : TEXCOORD0;
};
float4 main(PS_INPUT input) : SV_TARGET {
    float4 color = texRGB.Sample(samp, input.uv);
    if (swapRB == 1) return float4(color.b, color.g, color.r, 1.0);
    if (hasAlpha == 0) return float4(color.rgb, 1.0);
    return color;
}
)";

// ═══════════════════════════════════════════════════════════════

namespace muxplay {

bool ShaderManager::CompileAndCreateVertexShader(const std::string& name,
                                                  const char* source, const char* entry) {
    ComPtr<ID3DBlob> blob;
    ComPtr<ID3DBlob> errorBlob;

    HRESULT hr = D3DCompile(source, strlen(source), name.c_str(),
                             nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE,
                             entry, "vs_5_0",
                             D3DCOMPILE_OPTIMIZATION_LEVEL3, 0,
                             blob.GetAddressOf(), errorBlob.GetAddressOf());

    if (FAILED(hr)) {
        if (errorBlob) {
            SHADER_LOG("VS '%s' compile FAILED:\n%s", name.c_str(),
                       (const char*)errorBlob->GetBufferPointer());
        } else {
            SHADER_LOG("VS '%s' compile FAILED: hr=0x%08X", name.c_str(), (unsigned)hr);
        }
        return false;
    }

    ComPtr<ID3D11VertexShader> shader;
    hr = m_device->CreateVertexShader(blob->GetBufferPointer(), blob->GetBufferSize(),
                                       nullptr, shader.GetAddressOf());
    if (FAILED(hr)) {
        SHADER_LOG("VS '%s' CreateVertexShader FAILED: hr=0x%08X", name.c_str(), (unsigned)hr);
        return false;
    }

    m_vertexShaders[name] = std::move(shader);
    m_vsBlob = std::move(blob);
    return true;
}

bool ShaderManager::CompileAndCreatePixelShader(const std::string& name,
                                                 const char* source, const char* entry) {
    ComPtr<ID3DBlob> blob;
    ComPtr<ID3DBlob> errorBlob;

    HRESULT hr = D3DCompile(source, strlen(source), name.c_str(),
                             nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE,
                             entry, "ps_5_0",
                             D3DCOMPILE_OPTIMIZATION_LEVEL3, 0,
                             blob.GetAddressOf(), errorBlob.GetAddressOf());

    if (FAILED(hr)) {
        if (errorBlob) {
            SHADER_LOG("PS '%s' compile FAILED:\n%s", name.c_str(),
                       (const char*)errorBlob->GetBufferPointer());
        } else {
            SHADER_LOG("PS '%s' compile FAILED: hr=0x%08X", name.c_str(), (unsigned)hr);
        }
        return false;
    }

    ComPtr<ID3D11PixelShader> shader;
    hr = m_device->CreatePixelShader(blob->GetBufferPointer(), blob->GetBufferSize(),
                                      nullptr, shader.GetAddressOf());
    if (FAILED(hr)) {
        SHADER_LOG("PS '%s' CreatePixelShader FAILED: hr=0x%08X", name.c_str(), (unsigned)hr);
        return false;
    }

    m_pixelShaders[name] = std::move(shader);
    return true;
}

bool ShaderManager::Init(ID3D11Device* device) {
    if (!device) return false;
    m_device = device;

    // 编译 overlay 顶点着色器
    if (!CompileAndCreateVertexShader("common", g_vsCommon, "main")) return false;

    // 编译 overlay 像素着色器（核心）
    if (!CompileAndCreatePixelShader("text",  g_psText,  "main")) return false;
    if (!CompileAndCreatePixelShader("image", g_psImage, "main")) return false;

    // 编译视频像素着色器（备用，供未来多格式渲染使用）
    if (!CompileAndCreatePixelShader("nv12",   g_psNv12,   "main")) return false;
    if (!CompileAndCreatePixelShader("yuv420", g_psYuv420, "main")) return false;
    if (!CompileAndCreatePixelShader("rgb",    g_psRgb,    "main")) return false;

    // 创建 overlay 输入布局（float4 pos + float2 uv）
    D3D11_INPUT_ELEMENT_DESC layout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 0,  D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,       0, 16, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };

    HRESULT hr = m_device->CreateInputLayout(
        layout, _countof(layout),
        m_vsBlob->GetBufferPointer(), m_vsBlob->GetBufferSize(),
        m_inputLayout.GetAddressOf());
    if (FAILED(hr)) {
        SHADER_LOG("CreateInputLayout FAILED: hr=0x%08X", (unsigned)hr);
        return false;
    }

    SHADER_LOG("All shaders compiled successfully");
    return true;
}

ID3D11VertexShader* ShaderManager::GetVertexShader(const std::string& name) {
    auto it = m_vertexShaders.find(name);
    return (it != m_vertexShaders.end()) ? it->second.Get() : nullptr;
}

ID3D11PixelShader* ShaderManager::GetPixelShader(const std::string& name) {
    auto it = m_pixelShaders.find(name);
    return (it != m_pixelShaders.end()) ? it->second.Get() : nullptr;
}

ID3D11InputLayout* ShaderManager::GetInputLayout() {
    return m_inputLayout.Get();
}

} // namespace muxplay
