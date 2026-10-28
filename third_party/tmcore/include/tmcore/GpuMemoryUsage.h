#pragma once
#include "Export.h"

#include <cstdint>

namespace tmcore
{

// 全系统显存占用（PDH "GPU Adapter Memory" 计数器，Win10 1803+）
struct GpuMemoryInfo
{
    uint64_t dedicated_usage{ 0 };  // 专用显存已用（字节）
    uint64_t shared_usage{ 0 };     // 共享内存已用（字节）
    bool valid{ false };            // false：系统无该计数器或查询失败
};

/**
 * @brief 显存占用查询（全系统，按显卡 LUID）
 *
 * 计数器集 "\GPU Adapter Memory(*)" 从 Win10 1803 起提供，
 * 值为系统内所有进程的合计占用；LUID 来自 GpuInfo::luid。
 */
class TMCORE_API GpuMemoryUsage
{
public:
    GpuMemoryUsage();
    ~GpuMemoryUsage();
    GpuMemoryUsage(const GpuMemoryUsage&) = delete;
    GpuMemoryUsage& operator=(const GpuMemoryUsage&) = delete;

    // luid: GpuInfo::luid；传 0 统计所有适配器
    GpuMemoryInfo Query(uint64_t luid);

private:
    class Impl;
    Impl* m_impl;
};

} // namespace tmcore
