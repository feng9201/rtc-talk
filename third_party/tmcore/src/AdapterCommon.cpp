// 移植自 TrafficMonitor/AdapterCommon.cpp
// 改动：
//  1. 去除 stdafx.h / CCommon::StrToUnicode（改用 tmhelper）
//  2. 去除 CCommon::LoadText 多语言资源依赖（无连接时返回空列表，由调用方处理）
//  3. 源文件由 GBK 转为 UTF-8 编码
#include "AdapterCommon.h"
#include "StringHelper.h"

CAdapterCommon::CAdapterCommon()
{
}

CAdapterCommon::~CAdapterCommon()
{
}

void CAdapterCommon::GetAdapterInfo(std::vector<tmcore::NetworkConnection>& adapters)
{
    adapters.clear();
    PIP_ADAPTER_INFO pIpAdapterInfo = (PIP_ADAPTER_INFO)new BYTE[sizeof(IP_ADAPTER_INFO)];  // PIP_ADAPTER_INFO 结构体指针存储本机网卡信息
    unsigned long stSize = sizeof(IP_ADAPTER_INFO);  // 得到结构体大小，用于 GetAdaptersInfo 参数
    int nRel = GetAdaptersInfo(pIpAdapterInfo, &stSize);  // 调用 GetAdaptersInfo 函数，填充 pIpAdapterInfo 指针变量；其中 stSize 参数既是一个输入量也是一个输出量

    if (ERROR_BUFFER_OVERFLOW == nRel)
    {
        // 如果函数返回的是 ERROR_BUFFER_OVERFLOW
        // 则说明 GetAdaptersInfo 参数传递的内存空间不够，同时其传出 stSize，表示需要的空间大小
        // 这也是说明为什么 stSize 既是一个输入量也是一个输出量
        delete[] (BYTE*)pIpAdapterInfo;  // 释放原来的内存空间
        pIpAdapterInfo = (PIP_ADAPTER_INFO)new BYTE[stSize];  // 重新申请内存空间用来存储所有网卡信息
        nRel = GetAdaptersInfo(pIpAdapterInfo, &stSize);  // 再次调用 GetAdaptersInfo 函数，填充 pIpAdapterInfo 指针变量
    }

    PIP_ADAPTER_INFO pIpAdapterInfoHead = pIpAdapterInfo;  // 保存 pIpAdapterInfo 链表中第一个元素的地址
    if (ERROR_SUCCESS == nRel)
    {
        while (pIpAdapterInfo)
        {
            tmcore::NetworkConnection connection;
            connection.description = pIpAdapterInfo->Description;
            connection.ip_address = tmhelper::StrToUnicode(pIpAdapterInfo->IpAddressList.IpAddress.String);
            connection.subnet_mask = tmhelper::StrToUnicode(pIpAdapterInfo->IpAddressList.IpMask.String);
            connection.default_gateway = tmhelper::StrToUnicode(pIpAdapterInfo->GatewayList.IpAddress.String);

            adapters.push_back(connection);
            pIpAdapterInfo = pIpAdapterInfo->Next;
        }
    }
    // 释放内存空间
    if (pIpAdapterInfoHead)
    {
        delete[] (BYTE*)pIpAdapterInfoHead;
    }
}

void CAdapterCommon::RefreshIpAddress(std::vector<tmcore::NetworkConnection>& adapters)
{
    std::vector<tmcore::NetworkConnection> adapters_tmp;
    GetAdapterInfo(adapters_tmp);
    for (const auto& adapter_tmp : adapters_tmp)
    {
        for (auto& adapter : adapters)
        {
            if (adapter_tmp.description == adapter.description)
            {
                adapter.ip_address = adapter_tmp.ip_address;
                adapter.subnet_mask = adapter_tmp.subnet_mask;
                adapter.default_gateway = adapter_tmp.default_gateway;
            }
        }
    }
}

void CAdapterCommon::GetIfTableInfo(std::vector<tmcore::NetworkConnection>& adapters, MIB_IFTABLE* pIfTable)
{
    // 依次在 IfTable 里查找每个连接
    for (size_t i{}; i < adapters.size(); i++)
    {
        if (adapters[i].description.empty())
            continue;
        int index;
        index = FindConnectionInIfTable(adapters[i].description, pIfTable);
        if (index == -1)  // 如果使用精确匹配的方式没有找到，则采用模糊匹配的方式再查找一次
            index = FindConnectionInIfTableFuzzy(adapters[i].description, pIfTable);
        //if (index != -1)
        //{
        adapters[i].if_index = index;
        if (index >= 0)
        {
            adapters[i].in_bytes = pIfTable->table[index].dwInOctets;
            adapters[i].out_bytes = pIfTable->table[index].dwOutOctets;
            adapters[i].description_2 = (const char*)pIfTable->table[index].bDescr;
        }
        //}
    }
}

void CAdapterCommon::GetAllIfTableInfo(std::vector<tmcore::NetworkConnection>& adapters, MIB_IFTABLE* pIfTable)
{
    std::vector<tmcore::NetworkConnection> adapters_tmp;
    GetAdapterInfo(adapters_tmp);  // 获取 IP 地址
    adapters.clear();
    for (size_t i{}; i < pIfTable->dwNumEntries; i++)
    {
        tmcore::NetworkConnection connection;
        connection.description = connection.description_2 = (const char*)pIfTable->table[i].bDescr;
        connection.if_index = static_cast<int>(i);
        connection.in_bytes = pIfTable->table[i].dwInOctets;
        connection.out_bytes = pIfTable->table[i].dwOutOctets;
        for (size_t j{}; j < adapters_tmp.size(); j++)
        {
            if (connection.description.find(adapters_tmp[j].description) != std::string::npos)
            {
                connection.ip_address = adapters_tmp[j].ip_address;
                connection.subnet_mask = adapters_tmp[j].subnet_mask;
                connection.default_gateway = adapters_tmp[j].default_gateway;
                break;
            }
        }
        adapters.push_back(connection);
    }
}

int CAdapterCommon::FindConnectionInIfTable(std::string connection, MIB_IFTABLE* pIfTable)
{
    for (size_t i{}; i < pIfTable->dwNumEntries; i++)
    {
        std::string descr = (const char*)pIfTable->table[i].bDescr;
        if (descr == connection)
            return static_cast<int>(i);
    }
    return -1;
}

int CAdapterCommon::FindConnectionInIfTableFuzzy(std::string connection, MIB_IFTABLE* pIfTable)
{
    for (size_t i{}; i < pIfTable->dwNumEntries; i++)
    {
        std::string descr = (const char*)pIfTable->table[i].bDescr;
        size_t index;
        // 在较长的字符串里查找较短的字符串
        if (descr.size() >= connection.size())
            index = descr.find(connection);
        else
            index = connection.find(descr);
        if (index != std::string::npos)
            return static_cast<int>(i);
    }
    // 如果还是没有找到，则使用字符串匹配算法查找
    double max_degree{};
    int best_index{};
    for (size_t i{}; i < pIfTable->dwNumEntries; i++)
    {
        std::string descr = (const char*)pIfTable->table[i].bDescr;
        double degree = tmhelper::StringSimilarDegree_LD(descr, connection);
        if (degree > max_degree)
        {
            max_degree = degree;
            best_index = static_cast<int>(i);
        }
    }
    return best_index;
}
