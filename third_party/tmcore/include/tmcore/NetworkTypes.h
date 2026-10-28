#pragma once
#include <cstdint>
#include <string>
#include <vector>

// 确保 SDK 基础类型先于 iphlpapi.h 就绪（未定义 _WIN32_WINNT 时 SDK 头解析失败）
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif
#include <windows.h>
#include <iphlpapi.h>

namespace tmcore
{

// 网络连接信息（改编自 TrafficMonitor 的 NetWorkConection 结构）
struct NetworkConnection
{
    int if_index{ -1 };           // 在 MIB_IFTABLE 中的索引
    std::string description;     // 网卡描述（来自 GetAdaptersInfo）
    std::string description_2;   // 网卡描述（来自 GetIfTable）
    uint32_t in_bytes{ 0 };      // 刷新列表时已接收字节数
    uint32_t out_bytes{ 0 };     // 刷新列表时已发送字节数
    std::wstring ip_address{ L"-.-.-.-" };       // IP 地址
    std::wstring subnet_mask{ L"-.-.-.-" };     // 子网掩码
    std::wstring default_gateway{ L"-.-.-.-" }; // 默认网关
};

// 一次网速采样的结果
struct TrafficSample
{
    uint64_t in_speed{ 0 };   // 当前下行速度（字节/秒）
    uint64_t out_speed{ 0 };  // 当前上行速度（字节/秒）
    uint64_t in_bytes{ 0 };   // 当前累计已接收字节数
    uint64_t out_bytes{ 0 };   // 当前累计已发送字节数
};

// 内存信息
struct MemoryInfo
{
    int usage_percent{ 0 };    // 内存利用率（0-100）
    uint64_t total_phys{ 0 };  // 物理内存总量（字节）
    uint64_t avail_phys{ 0 };  // 可用物理内存（字节）
};

} // namespace tmcore
