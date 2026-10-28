// GpuMemoryUsage 实现
#include "tmcore/GpuMemoryUsage.h"

#include "pdh/PdhAdapterMemory.h"

namespace tmcore
{

class GpuMemoryUsage::Impl
{
public:
    CPdhAdapterMemory dedicated{ L"\\GPU Adapter Memory(*)\\Dedicated Usage" };
    CPdhAdapterMemory shared{ L"\\GPU Adapter Memory(*)\\Shared Usage" };
};

GpuMemoryUsage::GpuMemoryUsage()
    : m_impl(new Impl())
{
}

GpuMemoryUsage::~GpuMemoryUsage()
{
    delete m_impl;
}

GpuMemoryInfo GpuMemoryUsage::Query(uint64_t luid)
{
    GpuMemoryInfo info;
    const bool dedicated_ok = m_impl->dedicated.QueryLuid(luid, info.dedicated_usage);
    const bool shared_ok = m_impl->shared.QueryLuid(luid, info.shared_usage);
    info.valid = dedicated_ok || shared_ok;
    return info;
}

} // namespace tmcore
