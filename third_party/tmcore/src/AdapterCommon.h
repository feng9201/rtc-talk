#pragma once
// 移植自 TrafficMonitor/AdapterCommon.h
// 改动：去除 stdafx.h/Common.h/MFC 资源依赖，使用 tmcore::NetworkConnection
#include "tmcore/NetworkTypes.h"

class CAdapterCommon
{
public:
    CAdapterCommon();
    ~CAdapterCommon();

    // 获取网络适配器列表，包含每个连接的 IP 地址、子网掩码、默认网关信息
    static void GetAdapterInfo(std::vector<tmcore::NetworkConnection>& adapters);

    // 刷新网络适配器列表中的 IP 地址、子网掩码、默认网关信息
    static void RefreshIpAddress(std::vector<tmcore::NetworkConnection>& adapters);

    // 获取网络适配器列表中每个网络连接在 MIB_IFTABLE 中的索引和初始时已接收/发送字节数等信息
    static void GetIfTableInfo(std::vector<tmcore::NetworkConnection>& adapters, MIB_IFTABLE* pIfTable);

    // 直接将 MIB_IFTABLE 中的所有网络连接添加到 adapters 容器中
    static void GetAllIfTableInfo(std::vector<tmcore::NetworkConnection>& adapters, MIB_IFTABLE* pIfTable);

private:
    // 在一个字符串数组中判断该连接是否在 IfTable 列表里，如果找不到则返回 -1
    static int FindConnectionInIfTable(std::string connection, MIB_IFTABLE* pIfTable);

    // 在一个字符串数组中判断该连接是否在 IfTable 列表里，如果找不到则返回 -1，只需要字符串匹配
    static int FindConnectionInIfTableFuzzy(std::string connection, MIB_IFTABLE* pIfTable);
};
