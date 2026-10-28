#pragma once
#include "Export.h"
#include <string>
#include <vector>

namespace tmcore
{

/**
 * @brief 硬盘利用率监控（PDH "PhysicalDisk(*)\% Idle Time"）
 *
 * 移植自 TrafficMonitor 的 CPdhDiskUsage。
 */
class TMCORE_API DiskUsage
{
public:
    DiskUsage();
    ~DiskUsage();
    DiskUsage(const DiskUsage&) = delete;
    DiskUsage& operator=(const DiskUsage&) = delete;

    /**
     * @brief 获取指定物理磁盘的利用率（0-100）
     * @param disk_index  0=第一个物理磁盘, 1=第二个...
     * @return true=成功, false=失败（首次调用通常失败，需间隔 200ms 以上重试）
     */
    bool GetUsage(int disk_index, int& usage);

    // 获取物理磁盘名称列表（PDH 返回顺序，如 "0 C: D:"）
    std::vector<std::wstring> GetDiskNames() const;

    // 根据名称查找磁盘序号，找不到返回 -1
    int FindDiskIndex(const std::wstring& disk_name) const;

    // 磁盘监控是否可用（计数器是否存在）
    bool IsAvailable() const;

private:
    class Impl;
    Impl* m_impl;
};

} // namespace tmcore
