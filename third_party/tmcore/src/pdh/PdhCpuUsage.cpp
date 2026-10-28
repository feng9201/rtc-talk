// 移植自 TrafficMonitor/PdhHardwareQuery/CPUUsage.cpp（PDH 部分）
// 改动：Win 版本判断由 theApp 改为 tmhelper::IsWindows10OrGreater()
#include "PdhCpuUsage.h"
#include "../StringHelper.h"

CPdhCPUUsage::CPdhCPUUsage()
    : CPdhQuery(tmhelper::IsWindows10OrGreater()
        ? L"\\Processor Information(_Total)\\% Processor Utility"
        : L"\\Processor Information(_Total)\\% Processor Time")
{
}

bool CPdhCPUUsage::GetCPUUsage(int& cpu_usage)
{
    double value{};
    if (QueryValue(value))
    {
        cpu_usage = static_cast<int>(value);
        if (cpu_usage > 100)
            cpu_usage = 100;
        return true;
    }
    return false;
}
