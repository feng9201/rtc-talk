// MemoryUsage 实现
#include "tmcore/MemoryUsage.h"
#include <windows.h>

namespace tmcore
{

MemoryInfo MemoryUsage::Query()
{
    MEMORYSTATUSEX statex;
    statex.dwLength = sizeof(statex);
    GlobalMemoryStatusEx(&statex);

    MemoryInfo info;
    info.usage_percent = static_cast<int>(statex.dwMemoryLoad);
    info.total_phys = statex.ullTotalPhys;
    info.avail_phys = statex.ullAvailPhys;
    return info;
}

} // namespace tmcore
