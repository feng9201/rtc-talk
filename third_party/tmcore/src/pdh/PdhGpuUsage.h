#pragma once
// 移植自 TrafficMonitor/PdhHardwareQuery/GpuUsage.h
#include "PdhQuery.h"

// GPU 利用率监控类
class CPdhGPUUsage : public CPdhQuery
{
public:
    CPdhGPUUsage();
    ~CPdhGPUUsage();

    // 获取 GPU 利用率（0-100）
    // 返回值: true=成功, false=失败
    // 注意：首次调用通常返回 false（PDH 需要预热），建议间隔 200ms 以上再次调用
    bool GetGpuUsage(/*out*/ int& usage);
};
