// NetworkMonitor 实现
// 采样逻辑移植自 TrafficMonitor/TrafficMonitorDlg.cpp 的
// IniConnection() / AutoSelect() / DoMonitorTimerAcquisition() / GetConnectIfTable()
#include "tmcore/NetworkMonitor.h"
#include "AdapterCommon.h"
#include "StringHelper.h"
#include <cstdlib>
#include <iphlpapi.h>

namespace tmcore
{

NetworkMonitor::NetworkMonitor()
{
    // 构造时刷新连接列表并自动选择
    RefreshConnections(false);
    if (!m_connections.empty())
        AutoSelect();
}

NetworkMonitor::~NetworkMonitor()
{
    free(m_if_table);
}

const std::vector<NetworkConnection>& NetworkMonitor::RefreshConnections(bool all_interfaces)
{
    // 为 m_if_table 开辟所需大小的内存
    if (!QueryIfTable())
        return m_connections;

    // 获取当前所有的连接，并保存到 m_connections 容器中
    if (!all_interfaces)
    {
        m_connections.clear();
        CAdapterCommon::GetAdapterInfo(m_connections);
        CAdapterCommon::GetIfTableInfo(m_connections, m_if_table);
    }
    else
    {
        CAdapterCommon::GetAllIfTableInfo(m_connections, m_if_table);
    }

    if (m_selected >= static_cast<int>(m_connections.size()))
        m_selected = 0;
    m_connection_changed = true;

    return m_connections;
}

bool NetworkMonitor::Select(int index)
{
    if (index < 0 || index >= static_cast<int>(m_connections.size()))
        return false;
    m_selected = index;
    m_select_all = false;
    m_connection_changed = true;
    return true;
}

void NetworkMonitor::SelectAll()
{
    m_select_all = true;
    m_connection_changed = true;
}

void NetworkMonitor::AutoSelect()
{
    unsigned __int64 max_in_out_bytes{};
    unsigned __int64 in_out_bytes;
    m_selected = 0;
    // 自动选择连接时，查找已发送和已接收字节数之和最多的那个连接，并将其设置为当前查看的连接
    for (size_t i{}; i < m_connections.size(); i++)
    {
        auto table = GetConnectIfRow(static_cast<int>(i));
        if (table.dwOperStatus == IF_OPER_STATUS_OPERATIONAL)  // 只选择网络状态为正常的连接
        {
            in_out_bytes = static_cast<unsigned __int64>(table.dwInOctets) + table.dwOutOctets;
            if (in_out_bytes > max_in_out_bytes)
            {
                max_in_out_bytes = in_out_bytes;
                m_selected = static_cast<int>(i);
            }
        }
    }
    m_select_all = false;
    m_connection_changed = true;
}

int NetworkMonitor::SelectedIndex() const
{
    return m_select_all ? -1 : m_selected;
}

int NetworkMonitor::ConnectionCount() const
{
    return static_cast<int>(m_connections.size());
}

bool NetworkMonitor::GetConnectionInfo(int connection_index, NetworkConnection& info) const
{
    if (connection_index < 0 || connection_index >= static_cast<int>(m_connections.size()))
        return false;
    info = m_connections[connection_index];
    return true;
}

const TrafficSample& NetworkMonitor::Sample()
{
    QueryIfTable();

    unsigned __int64 in_bytes{};
    unsigned __int64 out_bytes{};
    if (!m_select_all)  // 获取当前选中连接的网速
    {
        auto table = GetConnectIfRow(m_selected);
        in_bytes = table.dwInOctets;
        out_bytes = table.dwOutOctets;
    }
    else  // 获取全部连接的网速
    {
        in_bytes = 0;
        out_bytes = 0;
        for (size_t i{}; i < m_connections.size(); i++)
        {
            auto table = GetConnectIfRow(static_cast<int>(i));
            in_bytes += table.dwInOctets;
            out_bytes += table.dwOutOctets;
        }
    }

    unsigned __int64 cur_in_speed{}, cur_out_speed{};  // 本次监控时间间隔内的上传和下载速度

    // 如果发送和接收的字节数为 0 或上次发送和接收的字节数为 0 或当前连接已改变时，网速无效
    if ((in_bytes == 0 && out_bytes == 0) || (m_last_in_bytes == 0 && m_last_out_bytes == 0) || m_connection_changed
        || m_last_in_bytes > in_bytes || m_last_out_bytes > out_bytes)
    {
        cur_in_speed = 0;
        cur_out_speed = 0;
    }
    else
    {
        cur_in_speed = in_bytes - m_last_in_bytes;
        cur_out_speed = out_bytes - m_last_out_bytes;
    }

    // 计算两次获取网速的时间间隔
    uint64_t net_speed_time = tmhelper::GetCurrentTimeMs();
    uint64_t time_span = 1000;
    if (m_last_sample_time != 0)
        time_span = net_speed_time - m_last_sample_time;
    m_last_sample_time = net_speed_time;

    // 将当前监控时间间隔的流量转换成每秒时间间隔内的流量
    m_sample.in_bytes = in_bytes;
    m_sample.out_bytes = out_bytes;
    m_sample.in_speed = time_span > 0 ? static_cast<unsigned __int64>(cur_in_speed * 1000 / time_span) : 0;
    m_sample.out_speed = time_span > 0 ? static_cast<unsigned __int64>(cur_out_speed * 1000 / time_span) : 0;

    m_last_in_bytes = in_bytes;
    m_last_out_bytes = out_bytes;
    m_connection_changed = false;

    return m_sample;
}

bool NetworkMonitor::GetIfRow(MIB_IFROW& row) const
{
    return GetIfRow(m_selected, row);
}

bool NetworkMonitor::GetIfRow(int connection_index, MIB_IFROW& row) const
{
    if (connection_index < 0 || connection_index >= static_cast<int>(m_connections.size()))
        return false;
    row = GetConnectIfRow(connection_index);
    return true;
}

void NetworkMonitor::Reinit()
{
    m_connection_changed = true;
    m_last_in_bytes = 0;
    m_last_out_bytes = 0;
    m_last_sample_time = 0;
    RefreshConnections(false);
}

bool NetworkMonitor::QueryIfTable()
{
    int rtn{};
    // 移植自原版的 SEH 保护（GetIfTable 偶发崩溃的处理）
    auto getIfTable = [&]()
    {
#if defined(_MSC_VER)
        __try
        {
            rtn = GetIfTable(m_if_table, &m_if_table_size, FALSE);
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            free(m_if_table);
            m_if_table_size = sizeof(MIB_IFTABLE);
            m_if_table = (MIB_IFTABLE*)malloc(m_if_table_size);
            rtn = GetIfTable(m_if_table, &m_if_table_size, FALSE);
        }
#else
        rtn = GetIfTable(m_if_table, &m_if_table_size, FALSE);
#endif
    };

    // 首次分配缓冲区
    if (m_if_table == nullptr)
    {
        m_if_table_size = sizeof(MIB_IFTABLE);
        m_if_table = (MIB_IFTABLE*)malloc(m_if_table_size);
    }

    getIfTable();
    if (rtn == ERROR_INSUFFICIENT_BUFFER)  // 缓冲区大小不够，用新的大小重新开辟一块内存
    {
        free(m_if_table);
        m_if_table = (MIB_IFTABLE*)malloc(m_if_table_size);
        rtn = GetIfTable(m_if_table, &m_if_table_size, FALSE);
    }

    return rtn == NO_ERROR;
}

MIB_IFROW NetworkMonitor::GetConnectIfRow(int connection_index) const
{
    if (connection_index >= 0 && connection_index < static_cast<int>(m_connections.size()))
    {
        int index = m_connections[connection_index].if_index;
        if (m_if_table != nullptr && index >= 0 && index < static_cast<int>(m_if_table->dwNumEntries))
            return m_if_table->table[index];
    }
    return MIB_IFROW();
}

} // namespace tmcore
