// 移植自 TrafficMonitor/PdhHardwareQuery/DiskUsage.cpp
// 改动：CString -> std::wstring
#include "PdhDiskUsage.h"
#include <algorithm>
#include <cmath>

CPdhDiskUsage::CPdhDiskUsage()
    : CPdhQuery(L"\\PhysicalDisk(*)\\% Idle Time")
{
    m_isAvailable = Initialize();
    if (m_isAvailable)
    {
        // 预热：触发 PDH 内部初始化（首次 QueryValues 会填充实例列表）
        std::vector<CounterValueItem> dummy;
        QueryValues(dummy);
        ExtractDiskNames();
    }
}

CPdhDiskUsage::~CPdhDiskUsage()
{
}

void CPdhDiskUsage::ExtractDiskNames()
{
    m_diskNames.clear();
    std::vector<CounterValueItem> values;

    if (!QueryValues(values))
        return;

    for (const auto& item : values)
    {
        m_diskNames.push_back(item.name);
    }
}

int CPdhDiskUsage::CalculateUtilization(double idleTime) const
{
    // 关键处理：NVMe/RAID 等多队列磁盘的空闲时间可能 >100%
    // 例如: 4 队列磁盘空闲时间=400% → 实际空闲=100% → 利用率=0%
    if (idleTime > 100.0)
        idleTime = 100.0;

    // 确保范围在 0-100
    idleTime = (std::max)(0.0, (std::min)(100.0, idleTime));

    // 利用率 = 100% - 空闲时间
    double utilization = 100.0 - idleTime;
    return static_cast<int>(utilization + 0.5);  // 四舍五入
}

bool CPdhDiskUsage::GetDiskUsage(int diskIndex, int& usage)
{
    usage = 0;
    if (!m_isAvailable)
        return false;

    std::vector<CounterValueItem> values;
    if (!QueryValues(values) || values.empty())
        return false;

    if (diskIndex >= 0 && diskIndex < static_cast<int>(values.size()))
    {
        usage = CalculateUtilization(values[diskIndex].value);
        return true;
    }
    return false;
}

int CPdhDiskUsage::FindDiskIndex(const std::wstring diskName)
{
    int disk_index = -1;
    for (int i = 0; i < static_cast<int>(m_diskNames.size()); i++)
    {
        if (diskName == m_diskNames[i])
        {
            disk_index = i;
            break;
        }
    }
    return disk_index;
}
