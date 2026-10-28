// DiskUsage 实现
#include "tmcore/DiskUsage.h"
#include "pdh/PdhDiskUsage.h"

namespace tmcore
{

class DiskUsage::Impl
{
public:
    CPdhDiskUsage query;
};

DiskUsage::DiskUsage()
    : m_impl(new Impl())
{
}

DiskUsage::~DiskUsage()
{
    delete m_impl;
}

bool DiskUsage::GetUsage(int disk_index, int& usage)
{
    return m_impl->query.GetDiskUsage(disk_index, usage);
}

std::vector<std::wstring> DiskUsage::GetDiskNames() const
{
    return m_impl->query.GetDiskNames();
}

int DiskUsage::FindDiskIndex(const std::wstring& disk_name) const
{
    return m_impl->query.FindDiskIndex(disk_name);
}

bool DiskUsage::IsAvailable() const
{
    return m_impl->query.IsAvailable();
}

} // namespace tmcore
