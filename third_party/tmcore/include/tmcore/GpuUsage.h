#pragma once
#include "Export.h"

namespace tmcore
{

/**
 * @brief 显卡利用率监控（PDH "GPU Engine" 计数器，Win10+）
 *
 * 移植自 TrafficMonitor 的 CPdhGPUUsage。
 */
class TMCORE_API GpuUsage
{
public:
    GpuUsage();
    ~GpuUsage();
    GpuUsage(const GpuUsage&) = delete;
    GpuUsage& operator=(const GpuUsage&) = delete;

    /**
     * @brief 获取 GPU 利用率（0-100，取所有引擎类型中的最大值，同任务管理器）
     * @return true=成功, false=失败（首次调用通常失败，间隔 200ms 以上再次调用）
     */
    bool GetUsage(int& usage);

private:
    class Impl;
    Impl* m_impl;
};

} // namespace tmcore
