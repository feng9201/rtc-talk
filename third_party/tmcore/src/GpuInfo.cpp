// GpuInfo 实现：DXGI 适配器枚举
#include "tmcore/GpuInfo.h"

#include <dxgi1_2.h>
#include <wrl/client.h>

#include <algorithm>

using Microsoft::WRL::ComPtr;

namespace tmcore
{

namespace
{

// 去重谓词：同一块显卡的多个显示输出在名称/厂商/专用显存上完全一致
bool SamePhysicalAdapter(const GpuInfo& a, const GpuInfo& b)
{
    return a.name == b.name && a.vendor_id == b.vendor_id
           && a.dedicated_video_memory == b.dedicated_video_memory;
}

} // namespace

std::vector<GpuInfo> GpuInfoQuery::Enumerate()
{
    // 同一块显卡因多个显示输出会枚举出多个同名适配器（不同 LUID），
    // 去重后保留 index 最小（主显示输出）的那个
    std::vector<GpuInfo> unique;
    for (GpuInfo& info : EnumerateAll())
    {
        const bool duplicated = std::any_of(unique.begin(), unique.end(),
                                            [&info](const GpuInfo& kept) {
                                                return SamePhysicalAdapter(kept, info);
                                            });
        if (!duplicated)
            unique.push_back(std::move(info));
    }
    return unique;
}

std::vector<GpuInfo> GpuInfoQuery::EnumerateAll()
{
    std::vector<GpuInfo> result;

    ComPtr<IDXGIFactory1> factory;
    if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), &factory)))
        return result;

    for (uint32_t i = 0;; ++i)
    {
        ComPtr<IDXGIAdapter1> adapter;
        const HRESULT hr = factory->EnumAdapters1(i, &adapter);
        if (FAILED(hr))   // DXGI_ERROR_NOT_FOUND = 枚举结束
            break;

        DXGI_ADAPTER_DESC1 desc{};
        if (FAILED(adapter->GetDesc1(&desc)))
            continue;

        GpuInfo info;
        info.index = i;
        info.name = desc.Description;   // WCHAR[128]
        info.dedicated_video_memory = static_cast<uint64_t>(desc.DedicatedVideoMemory);
        info.shared_system_memory = static_cast<uint64_t>(desc.SharedSystemMemory);
        info.vendor_id = desc.VendorId;
        info.luid = (static_cast<uint64_t>(static_cast<uint32_t>(desc.AdapterLuid.HighPart)) << 32)
                    | static_cast<uint32_t>(desc.AdapterLuid.LowPart);
        info.is_software_adapter = (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0;
        result.push_back(std::move(info));
    }

    return result;
}

bool GpuInfoQuery::Primary(GpuInfo& info)
{
    for (const GpuInfo& gpu : Enumerate())
    {
        if (gpu.is_software_adapter)
            continue;
        info = gpu;
        return true;
    }
    return false;
}

bool GpuInfoQuery::Find(uint64_t luid, GpuInfo& info)
{
    for (const GpuInfo& gpu : Enumerate())
    {
        if (gpu.luid != luid)
            continue;
        info = gpu;
        return true;
    }
    return false;
}

} // namespace tmcore
