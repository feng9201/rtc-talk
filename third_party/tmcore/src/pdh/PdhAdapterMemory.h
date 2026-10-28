#pragma once
#include "PdhQuery.h"

#include <cstdint>
#include <string>

// 显存占用查询（PDH "GPU Adapter Memory" 计数器集，Win10 1803+）
// 计数器实例名形如 luid_0x00000000_0x0000DCA2_phys_0，按 LUID 聚合
class CPdhAdapterMemory : public CPdhQuery
{
public:
    explicit CPdhAdapterMemory(const wchar_t* full_counter_path)
        : CPdhQuery(full_counter_path)
    {
    }

    // luid=0 时统计所有适配器；usage 单位字节
    bool QueryLuid(uint64_t luid, uint64_t& usage);
};
