#pragma once
#include "Export.h"

#include <cstdint>
#include <string>
#include <vector>

namespace tmcore
{

/**
 * @brief 显卡信息（DXGI 枚举结果）
 */
struct GpuInfo
{
    // DXGI 适配器索引，可直接传给 CreateD3D11Device
    uint32_t index{ 0 };
    // 显卡名称，如 "NVIDIA GeForce RTX 3060"
    std::wstring name;
    // 专用显存总量（字节，DXGI_ADAPTER_DESC1::DedicatedVideoMemory）
    uint64_t dedicated_video_memory{ 0 };
    // 共享系统内存上限（字节，DXGI_ADAPTER_DESC1::SharedSystemMemory）
    uint64_t shared_system_memory{ 0 };
    // 厂商 ID：0x10DE=NVIDIA / 0x8086=Intel / 0x1002=AMD / 0x1414=Microsoft
    uint32_t vendor_id{ 0 };
    // 适配器 LUID（高 32 位 << 32 | 低 32 位），用于匹配 PDH 计数器，如 GpuMemoryUsage
    uint64_t luid{ 0 };
    // 是否软件适配器（WARP、Microsoft Basic Render Driver），展示列表时通常应跳过
    bool is_software_adapter{ false };
};

/**
 * @brief 显卡信息查询（DXGI 枚举，Win7+）
 *
 * 典型用法：
 *   tmcore::GpuInfo primary;
 *   if (tmcore::GpuInfoQuery::Primary(primary))
 *       // primary.name / primary.dedicated_video_memory / primary.luid ...
 *
 *   for (const auto& gpu : tmcore::GpuInfoQuery::Enumerate())
 *       // 多显卡列表
 */
class TMCORE_API GpuInfoQuery
{
public:
    // 枚举显卡列表（已去重）：同一块显卡因多个显示输出产生的重复项只保留 index 最小的一个
    static std::vector<GpuInfo> Enumerate();

    // 枚举 DXGI 原始适配器列表（不去重，同一块显卡可能因多个显示输出出现多次）
    static std::vector<GpuInfo> EnumerateAll();

    // 主显卡：去重列表中第一个非软件适配器；无可用显卡时返回 false
    static bool Primary(GpuInfo& info);

    // 按 LUID 查找（在去重列表中）；未找到返回 false
    static bool Find(uint64_t luid, GpuInfo& info);
};

} // namespace tmcore
