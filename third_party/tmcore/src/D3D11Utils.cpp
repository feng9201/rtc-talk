// CreateD3D11Device 实现：指定适配器创建 D3D11 设备（标志逐级回退）
#include "tmcore/D3D11Utils.h"

#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

using Microsoft::WRL::ComPtr;

namespace tmcore
{

bool CreateD3D11Device(int gpu_index,
                       ID3D11Device** out_device,
                       ID3D11DeviceContext** out_context)
{
    if (!out_device || !out_context)
        return false;
    *out_device = nullptr;
    *out_context = nullptr;

    ComPtr<IDXGIAdapter1> selected_adapter;
    if (gpu_index >= 0)
    {
        ComPtr<IDXGIFactory1> factory;
        if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), &factory)))
            return false;

        if (FAILED(factory->EnumAdapters1(static_cast<UINT>(gpu_index), &selected_adapter)))
            return false;
    }
    // gpu_index < 0 → selected_adapter 为空 → D3D11CreateDevice 使用默认适配器

    // 标志组合按可用性降级：
    //   VIDEO_SUPPORT : D3D11VA 硬件解码需要
    //   BGRA_SUPPORT  : WGL interop 共享纹理需要
    // 两者同时使用在部分驱动组合下会失败，因此逐级回退
    const UINT flag_combos[] = {
        D3D11_CREATE_DEVICE_VIDEO_SUPPORT | D3D11_CREATE_DEVICE_BGRA_SUPPORT,
        D3D11_CREATE_DEVICE_VIDEO_SUPPORT,
        D3D11_CREATE_DEVICE_BGRA_SUPPORT,
        0,
    };
    const D3D_FEATURE_LEVEL feature_levels[] = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
    };

    for (UINT flags : flag_combos)
    {
        D3D_FEATURE_LEVEL feature_level = D3D_FEATURE_LEVEL_11_0;
        const HRESULT hr = D3D11CreateDevice(
            selected_adapter.Get(),
            selected_adapter ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE,
            nullptr,   // 无软件光栅化器
            flags,
            feature_levels, ARRAYSIZE(feature_levels),
            D3D11_SDK_VERSION,
            out_device,
            &feature_level,
            out_context);
        if (SUCCEEDED(hr))
            return true;

        // 释放可能的部分成功对象
        if (*out_context) { (*out_context)->Release(); *out_context = nullptr; }
        if (*out_device) { (*out_device)->Release(); *out_device = nullptr; }
    }

    return false;
}

} // namespace tmcore
