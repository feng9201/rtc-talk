// GpuUsage 实现
#include "tmcore/GpuUsage.h"
#include "pdh/PdhGpuUsage.h"

namespace tmcore
{

class GpuUsage::Impl
{
public:
    CPdhGPUUsage query;
};

GpuUsage::GpuUsage()
    : m_impl(new Impl())
{
}

GpuUsage::~GpuUsage()
{
    delete m_impl;
}

bool GpuUsage::GetUsage(int& usage)
{
    return m_impl->query.GetGpuUsage(usage);
}

} // namespace tmcore
