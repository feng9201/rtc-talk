// tmcore 控制台验证程序
// 每 2 秒打印一次：网速 / CPU / 内存 / 显卡利用率 / CPU频率 / 硬盘利用率 / 连接详情
#include <tmcore/CpuFreq.h>
#include <tmcore/CpuUsage.h>
#include <tmcore/DiskUsage.h>
#include <tmcore/GpuUsage.h>
#include <tmcore/MemoryUsage.h>
#include <tmcore/NetworkMonitor.h>

#include <cstdio>
#include <fcntl.h>
#include <io.h>
#include <string>

static std::wstring FormatSpeed(uint64_t bytes_per_sec)
{
    wchar_t buf[64]{};
    if (bytes_per_sec < 1024)
        swprintf_s(buf, L"%llu B/s", bytes_per_sec);
    else if (bytes_per_sec < 1024 * 1024)
        swprintf_s(buf, L"%.2f KB/s", bytes_per_sec / 1024.0);
    else
        swprintf_s(buf, L"%.2f MB/s", bytes_per_sec / 1024.0 / 1024.0);
    return buf;
}

int main()
{
    // 控制台以 UTF-16 模式输出宽字符（中文正常显示）
    _setmode(_fileno(stdout), _O_U16TEXT);

    using namespace tmcore;

    NetworkMonitor monitor;
    CpuUsage cpu_usage;
    GpuUsage gpu_usage;
    CpuFreq cpu_freq;
    DiskUsage disk_usage;

    // ---- 连接列表与详情 ----
    const auto& connections = monitor.RefreshConnections();
    wprintf(L"==== 网络连接列表（%d 个）====\n", monitor.ConnectionCount());
    for (int i = 0; i < monitor.ConnectionCount(); i++)
    {
        const auto& conn = connections[i];
        wprintf(L"  [%d] %hs\n", i, conn.description.c_str());
        wprintf(L"      IP: %ls  掩码: %ls  网关: %ls\n",
                conn.ip_address.c_str(), conn.subnet_mask.c_str(), conn.default_gateway.c_str());
    }

    MIB_IFROW row{};
    if (monitor.GetIfRow(row))
    {
        wprintf(L"==== 当前连接详情（MIB_IFROW）====\n");
        wchar_t phys_addr[32]{};
        for (DWORD j = 0; j < row.dwPhysAddrLen && j < 8; j++)
            swprintf_s(phys_addr + j * 3, 4, L"%02X-", row.bPhysAddr[j]);
        wprintf(L"  物理地址: %ls  MTU: %lu  链路速度: %lu bps  状态: %lu\n",
                phys_addr, row.dwMtu, row.dwSpeed, row.dwOperStatus);
        wprintf(L"  已接收: %lu 字节  已发送: %lu 字节\n", row.dwInOctets, row.dwOutOctets);
    }

    if (disk_usage.IsAvailable())
    {
        wprintf(L"==== 物理磁盘：");
        for (const auto& name : disk_usage.GetDiskNames())
            wprintf(L" [%ls]", name.c_str());
        wprintf(L" ====\n");
    }

    // ---- 周期采样（首次 GPU/频率/硬盘查询可能失败，属正常预热现象）----
    wprintf(L"==== 每 2 秒采样一次，Ctrl+C 退出 ====\n");
    for (int n = 0;; n++)
    {
        const auto& sample = monitor.Sample();
        int cpu = cpu_usage.GetUsage();
        int gpu{};
        bool gpu_ok = gpu_usage.GetUsage(gpu);
        float freq{};
        bool freq_ok = cpu_freq.GetFreq(freq);
        int disk0{};
        bool disk_ok = disk_usage.GetUsage(0, disk0);
        auto mem = MemoryUsage::Query();

        wprintf(L"CPU:%3d%%  内存:%3d%%  GPU:%ls  频率:%ls  磁盘0:%ls  ↓%ls  ↑%ls\n",
                cpu, mem.usage_percent,
                gpu_ok ? std::to_wstring(gpu).append(L"%").c_str() : L"--",
                freq_ok ? std::to_wstring(freq).substr(0, 4).append(L"GHz").c_str() : L"--",
                disk_ok ? std::to_wstring(disk0).append(L"%").c_str() : L"--",
                FormatSpeed(sample.in_speed).c_str(), FormatSpeed(sample.out_speed).c_str());
        (void)n;
        Sleep(2000);
    }
}
