#pragma once
// 移植自 TrafficMonitor/PdhHardwareQuery/CPUUsage.h（PDH 部分拆分到此类）
#include "PdhQuery.h"

class CPdhCPUUsage : public CPdhQuery
{
public:
    CPdhCPUUsage();

    ~CPdhCPUUsage()
    {
    }

    bool GetCPUUsage(int& cpu_usage);
};
