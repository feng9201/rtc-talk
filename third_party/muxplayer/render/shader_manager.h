#pragma once

#include <d3d11.h>
#include <wrl/client.h>
#include <string>
#include <unordered_map>

namespace muxplay {

using Microsoft::WRL::ComPtr;

// ── Shader 编译与缓存管理 ──
// 集中管理所有 D3D11 shader 的编译（内联 HLSL，无外部 .cso 依赖）。
// 包含视频渲染 shader（NV12/YUV/RGB）和 overlay shader（text/image）。
class ShaderManager {
public:
    ShaderManager() = default;
    ~ShaderManager() = default;

    // 初始化：编译全部 shader 并缓存（仅调用一次）
    bool Init(ID3D11Device* device);

    // 查询已编译的 shader
    ID3D11VertexShader* GetVertexShader(const std::string& name);
    ID3D11PixelShader*  GetPixelShader(const std::string& name);

    // overlay 顶点输入布局（float4 pos + float2 uv）
    ID3D11InputLayout*  GetInputLayout();

private:
    bool CompileAndCreateVertexShader(const std::string& name,
                                       const char* source, const char* entry);
    bool CompileAndCreatePixelShader(const std::string& name,
                                      const char* source, const char* entry);

    ID3D11Device* m_device = nullptr;

    std::unordered_map<std::string, ComPtr<ID3D11VertexShader>> m_vertexShaders;
    std::unordered_map<std::string, ComPtr<ID3D11PixelShader>>  m_pixelShaders;
    ComPtr<ID3D11InputLayout> m_inputLayout;
    ComPtr<ID3DBlob> m_vsBlob;
};

} // namespace muxplay
