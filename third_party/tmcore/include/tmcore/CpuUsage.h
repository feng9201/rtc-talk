#pragma once
#include "Export.h"

namespace tmcore
{

/**
 * @brief CPU 利用率监控
 *
 * 移植自 TrafficMonitor 的 CCPUUsage。
 */
class TMCORE_API CpuUsage
{
public:
    CpuUsage();
    ~CpuUsage();
    CpuUsage(const CpuUsage&) = delete;
    CpuUsage& operator=(const CpuUsage&) = delete;

    /**
     * @brief 获取 CPU 利用率（0-100）
     * @param use_cpu_time  true：基于 CPU 时间（GetSystemTimes），与任务管理器一致性较好；
     *                       false：基于 PDH 性能计数器（失败时自动回退到 GetSystemTimes）
     */
    int GetUsage(bool use_cpu_time = false);

private:
    class Impl;
    Impl* m_impl;
};

} // namespace tmcore
