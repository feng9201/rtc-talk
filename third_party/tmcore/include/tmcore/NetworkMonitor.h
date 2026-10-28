#pragma once
#include "Export.h"
#include "NetworkTypes.h"

namespace tmcore
{

/**
 * @brief 网络监控类：连接枚举、网速采样、网络详细信息
 *
 * 典型用法：
 *   tmcore::NetworkMonitor monitor;
 *   auto& conns = monitor.RefreshConnections();   // 枚举网卡
 *   monitor.AutoSelect();                          // 自动选择流量最大的连接
 *   // 之后在定时器中（建议 >= 1 秒间隔）：
 *   const auto& s = monitor.Sample();              // 返回上/下行速度与累计字节数
 */
class TMCORE_API NetworkMonitor
{
public:
    NetworkMonitor();
    ~NetworkMonitor();
    NetworkMonitor(const NetworkMonitor&) = delete;
    NetworkMonitor& operator=(const NetworkMonitor&) = delete;

    // -------------------- 连接管理 --------------------

    // 刷新连接列表。all_interfaces=true 返回所有网络接口（含虚拟网卡等），
    // false 时仅返回 GetAdaptersInfo 中的网卡。返回对内部列表的引用。
    const std::vector<NetworkConnection>& RefreshConnections(bool all_interfaces = false);

    // 监控指定连接（index 为 RefreshConnections 返回列表的下标）
    bool Select(int index);

    // 监控全部连接（网速为所有连接之和）
    void SelectAll();

    // 自动选择：选择当前累计收发字节数最多的、状态正常的连接
    void AutoSelect();

    // 当前选中的连接下标；SelectAll 模式返回 -1
    int SelectedIndex() const;

    // 当前连接数量
    int ConnectionCount() const;

    // 获取指定连接的信息（IP/掩码/网关等）
    bool GetConnectionInfo(int connection_index, NetworkConnection& info) const;

    // -------------------- 网速采样 --------------------
    //
    // 采样一次并计算上/下行速度（字节/秒）。
    // 两次采样之间应有足够间隔（建议 >= 1 秒）；
    // 首次采样、连接切换或计数器回绕时速度为 0，之后恢复正常。
    const TrafficSample& Sample();

    // -------------------- 网络详细信息 --------------------

    // 获取当前选中连接的详细信息（物理地址、MTU、链路速度、运行状态、收发字节数/包数等）
    bool GetIfRow(MIB_IFROW& row) const;

    // 获取指定连接（下标）的详细信息
    bool GetIfRow(int connection_index, MIB_IFROW& row) const;

    // -------------------- 其他 --------------------

    // 重置内部状态（睡眠唤醒、网络配置变化后调用）
    void Reinit();

private:
    // 查询 MIB_IFTABLE（自动处理缓冲区大小）
    bool QueryIfTable();

    // 取指定连接对应的 MIB_IFROW（下标为连接列表下标）
    MIB_IFROW GetConnectIfRow(int connection_index) const;

private:
    MIB_IFTABLE* m_if_table{ nullptr };
    DWORD m_if_table_size{ 0 };

    std::vector<NetworkConnection> m_connections;
    int m_selected{ 0 };
    bool m_select_all{ false };
    bool m_connection_changed{ true };

    uint64_t m_in_bytes{ 0 };
    uint64_t m_out_bytes{ 0 };
    uint64_t m_last_in_bytes{ 0 };
    uint64_t m_last_out_bytes{ 0 };
    uint64_t m_last_sample_time{ 0 };

    TrafficSample m_sample;
};

} // namespace tmcore
