#pragma once
#include <QWidget>
#include <tmcore/CpuFreq.h>
#include <tmcore/CpuUsage.h>
#include <tmcore/DiskUsage.h>
#include <tmcore/GpuUsage.h>
#include <tmcore/MemoryUsage.h>
#include <tmcore/NetworkMonitor.h>

class QLabel;
class QComboBox;
class QTimer;

/**
 * @brief tmcore Qt 验证窗口
 *
 * 每 1 秒刷新一次监控数据：
 *   - CPU / 内存 / 显卡利用率、CPU 频率、硬盘利用率
 *   - 网速（上/下行）
 *   - 网络连接切换、连接详情（MIB_IFROW）
 */
class MainWidget : public QWidget
{
    Q_OBJECT

public:
    explicit MainWidget(QWidget* parent = nullptr);

private slots:
    void onTimer();
    void onConnectionChanged(int index);
    void showConnectionInfo();

private:
    void refreshConnectionList();

private:
    // 监控对象
    tmcore::NetworkMonitor m_monitor;
    tmcore::CpuUsage m_cpuUsage;
    tmcore::GpuUsage m_gpuUsage;
    tmcore::CpuFreq m_cpuFreq;
    tmcore::DiskUsage m_diskUsage;

    // UI
    QComboBox* m_connections{ nullptr };
    QLabel* m_cpuLabel{ nullptr };
    QLabel* m_memLabel{ nullptr };
    QLabel* m_gpuLabel{ nullptr };
    QLabel* m_freqLabel{ nullptr };
    QLabel* m_diskLabel{ nullptr };
    QLabel* m_netLabel{ nullptr };
    QTimer* m_timer{ nullptr };
};
