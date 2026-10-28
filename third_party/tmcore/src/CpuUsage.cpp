// CpuUsage 实现，移植自 TrafficMonitor/PdhHardwareQuery/CPUUsage.cpp
#include "tmcore/CpuUsage.h"
#include "StringHelper.h"
#include "pdh/PdhCpuUsage.h"
#include <sysinfoapi.h>

namespace tmcore
{

class CpuUsage::Impl
{
public:
    Impl() = default;
    ~Impl() = default;

    int GetUsage(bool use_cpu_time)
    {
        if (use_cpu_time)
        {
            return GetCpuUsageByGetSystemTimes();
        }
        else
        {
            int cpu_usage{};
            // 如果通过 pdh 获取 CPU 利用率失败，采用 GetSystemTimes 获取
            if (!m_pdh_cpu_usage_query.GetCPUUsage(cpu_usage))
            {
                cpu_usage = GetCpuUsageByGetSystemTimes();
            }
            return cpu_usage;
        }
    }

private:
    int GetCpuUsageByGetSystemTimes()
    {
        int cpu_usage{};
        FILETIME idleTime;
        FILETIME kernelTime;
        FILETIME userTime;
        GetSystemTimes(&idleTime, &kernelTime, &userTime);

        __int64 idle = tmhelper::CompareFileTime2(m_preidleTime, idleTime);
        __int64 kernel = tmhelper::CompareFileTime2(m_prekernelTime, kernelTime);
        __int64 user = tmhelper::CompareFileTime2(m_preuserTime, userTime);

        if (kernel + user == 0)
        {
            cpu_usage = 0;
        }
        else
        {
            // （总的时间 - 空闲时间）/ 总的时间 = 占用 CPU 的时间就是使用率
            cpu_usage = static_cast<int>(abs((kernel + user - idle) * 100 / (kernel + user)));
        }

        m_preidleTime = idleTime;
        m_prekernelTime = kernelTime;
        m_preuserTime = userTime;

        if (cpu_usage > 100)
            cpu_usage = 100;
        return cpu_usage;
    }

private:
    FILETIME m_preidleTime{};
    FILETIME m_prekernelTime{};
    FILETIME m_preuserTime{};

    CPdhCPUUsage m_pdh_cpu_usage_query;
};

CpuUsage::CpuUsage()
    : m_impl(new Impl())
{
}

CpuUsage::~CpuUsage()
{
    delete m_impl;
}

int CpuUsage::GetUsage(bool use_cpu_time)
{
    return m_impl->GetUsage(use_cpu_time);
}

} // namespace tmcore
