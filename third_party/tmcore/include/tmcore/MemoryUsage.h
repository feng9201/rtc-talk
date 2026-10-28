#pragma once
#include "Export.h"
#include "NetworkTypes.h"

namespace tmcore
{

/**
 * @brief 内存利用率监控（GlobalMemoryStatusEx）
 */
class TMCORE_API MemoryUsage
{
public:
    // 获取内存利用率及详细信息
    static MemoryInfo Query();
};

} // namespace tmcore
