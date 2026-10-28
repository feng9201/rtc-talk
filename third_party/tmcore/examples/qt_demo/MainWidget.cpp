#include "MainWidget.h"
#include <QtWidgets/QComboBox>
#include <QtWidgets/QGridLayout>
#include <QtWidgets/QGroupBox>
#include <QtWidgets/QLabel>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QPushButton>
#include <QTimer>

MainWidget::MainWidget(QWidget* parent)
    : QWidget(parent)
{
    setWindowTitle(QStringLiteral("tmcore Qt 验证程序"));
    resize(480, 320);

    auto* layout = new QGridLayout(this);

    // 连接选择
    m_connections = new QComboBox(this);
    auto* info_btn = new QPushButton(QStringLiteral("连接详情"), this);
    auto* conn_layout = new QHBoxLayout();
    conn_layout->addWidget(m_connections, 1);
    conn_layout->addWidget(info_btn);
    layout->addLayout(conn_layout, 0, 0, 1, 2);

    // 监控数据
    m_cpuLabel = new QLabel(QStringLiteral("CPU: --"), this);
    m_memLabel = new QLabel(QStringLiteral("内存: --"), this);
    m_gpuLabel = new QLabel(QStringLiteral("显卡: --"), this);
    m_freqLabel = new QLabel(QStringLiteral("CPU 频率: --"), this);
    m_diskLabel = new QLabel(QStringLiteral("磁盘0: --"), this);
    m_netLabel = new QLabel(QStringLiteral("网速: --"), this);

    layout->addWidget(m_cpuLabel, 1, 0);
    layout->addWidget(m_memLabel, 1, 1);
    layout->addWidget(m_gpuLabel, 2, 0);
    layout->addWidget(m_freqLabel, 2, 1);
    layout->addWidget(m_diskLabel, 3, 0);
    layout->addWidget(m_netLabel, 3, 1);

    refreshConnectionList();

    connect(m_connections, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &MainWidget::onConnectionChanged);
    connect(info_btn, &QPushButton::clicked, this, &MainWidget::showConnectionInfo);

    // 首次查询 GPU/频率/硬盘通常会失败（PDH 预热），先预热一次
    int gpu{};
    m_gpuUsage.GetUsage(gpu);
    float freq{};
    m_cpuFreq.GetFreq(freq);
    int disk{};
    m_diskUsage.GetUsage(0, disk);
    m_monitor.Sample();

    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, this, &MainWidget::onTimer);
    m_timer->start(1000);
}

void MainWidget::refreshConnectionList()
{
    QSignalBlocker blocker(m_connections);
    m_connections->clear();
    const auto& conns = m_monitor.RefreshConnections();
    for (int i = 0; i < m_monitor.ConnectionCount(); i++)
    {
        m_connections->addItem(QString::fromStdString(conns[i].description));
    }
    // AutoSelect
    m_monitor.AutoSelect();
    if (m_monitor.SelectedIndex() >= 0 && m_monitor.SelectedIndex() < m_connections->count())
        m_connections->setCurrentIndex(m_monitor.SelectedIndex());
}

void MainWidget::onConnectionChanged(int index)
{
    if (index >= 0)
        m_monitor.Select(index);
}

void MainWidget::onTimer()
{
    const auto& sample = m_monitor.Sample();

    int cpu = m_cpuUsage.GetUsage();
    auto mem = tmcore::MemoryUsage::Query();
    m_cpuLabel->setText(QStringLiteral("CPU: %1%").arg(cpu));
    m_memLabel->setText(QStringLiteral("内存: %1% (可用 %2 GB)")
                            .arg(mem.usage_percent)
                            .arg(mem.avail_phys / 1024.0 / 1024 / 1024, 0, 'f', 1));

    int gpu{};
    m_gpuLabel->setText(m_gpuUsage.GetUsage(gpu)
                            ? QStringLiteral("显卡: %1%").arg(gpu)
                            : QStringLiteral("显卡: --"));

    float freq{};
    m_freqLabel->setText(m_cpuFreq.GetFreq(freq)
                             ? QStringLiteral("CPU 频率: %1 GHz").arg(freq, 0, 'f', 2)
                             : QStringLiteral("CPU 频率: --"));

    int disk{};
    m_diskLabel->setText(m_diskUsage.GetUsage(0, disk)
                             ? QStringLiteral("磁盘0: %1%").arg(disk)
                             : QStringLiteral("磁盘0: --"));

    // 网速换算为比特单位（×8，十进制千，运营商惯例）
    auto fmtSpeed = [](uint64_t bytes_per_sec) {
        double bits = bytes_per_sec * 8.0;
        if (bits < 1000)
            return QStringLiteral("%1 bps").arg(bits, 0, 'f', 0);
        if (bits < 1000 * 1000)
            return QStringLiteral("%1 kbps").arg(bits / 1000.0, 0, 'f', 1);
        if (bits < 1000 * 1000 * 1000)
            return QStringLiteral("%1 Mbps").arg(bits / 1000.0 / 1000, 0, 'f', 2);
        return QStringLiteral("%1 Gbps").arg(bits / 1000.0 / 1000 / 1000, 0, 'f', 2);
    };
    m_netLabel->setText(QStringLiteral("↓ %1    ↑ %2")
                            .arg(fmtSpeed(sample.in_speed))
                            .arg(fmtSpeed(sample.out_speed)));
}

void MainWidget::showConnectionInfo()
{
    int index = m_connections->currentIndex();
    if (index < 0)
        return;

    tmcore::NetworkConnection conn;
    MIB_IFROW row{};
    QString text;
    if (m_monitor.GetConnectionInfo(index, conn))
    {
        text += QStringLiteral("描述: %1\n").arg(QString::fromStdString(conn.description));
        text += QStringLiteral("IP 地址: %1\n").arg(QString::fromStdWString(conn.ip_address));
        text += QStringLiteral("子网掩码: %1\n").arg(QString::fromStdWString(conn.subnet_mask));
        text += QStringLiteral("默认网关: %1\n").arg(QString::fromStdWString(conn.default_gateway));
    }
    if (m_monitor.GetIfRow(index, row))
    {
        QString phys;
        for (DWORD j = 0; j < row.dwPhysAddrLen && j < 8; j++)
            phys += QStringLiteral("%1-").arg(row.bPhysAddr[j], 2, 16, QChar('0'));
        if (phys.endsWith(QLatin1Char('-')))
            phys.chop(1);
        text += QStringLiteral("\n物理地址: %1\n").arg(phys);
        text += QStringLiteral("MTU: %1\n").arg(row.dwMtu);
        text += QStringLiteral("链路速度: %1 bps\n").arg(row.dwSpeed);
        text += QStringLiteral("状态: %1\n").arg(row.dwOperStatus == IF_OPER_STATUS_OPERATIONAL
                                                   ? QStringLiteral("正常")
                                                   : QStringLiteral("异常"));
        text += QStringLiteral("已接收: %1 字节\n").arg(row.dwInOctets);
        text += QStringLiteral("已发送: %1 字节").arg(row.dwOutOctets);
    }
    QMessageBox::information(this, QStringLiteral("连接详情"), text);
}
