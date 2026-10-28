# tmcore

从 [TrafficMonitor](https://github.com/zhongyang219/TrafficMonitor) 剥离的纯 C++17 系统监控库（无 MFC / 无 .NET 依赖），提供以下功能：

- 网络监控：网速采样（上/下行）、连接枚举、自动选择、监控全部连接
- CPU / 内存利用率
- 显卡利用率（PDH GPU Engine 计数器）
- CPU 频率、硬盘利用率（附加功能）
- 网络详细信息：IP / 子网掩码 / 默认网关 / 物理地址 / MTU / 链路速度 / 收发字节数等

## 目录结构

```
tmcore/
├── include/tmcore/     # 对外公共头（调用方只需 include 此目录）
├── src/                # 内部实现（PDH 采集层、AdapterCommon、pImpl）
├── examples/
│   ├── console_demo/   # 控制台验证程序
│   └── qt_demo/        # Qt5 QWidget 验证程序
└── CMakeLists.txt
```

## 构建

| CMake 选项 | 默认值 | 说明 |
|------------|--------|------|
| `TMCORE_BUILD_SHARED` | `OFF` | `ON` 时构建动态库（`tmcore.dll`），默认静态库（`tmcore.lib`） |
| `TMCORE_BUILD_EXAMPLES` | `ON` | 构建示例程序（Qt 示例在未找到 Qt5 时自动跳过） |

```bash
# 默认构建静态库 + 示例
cmake -B build
cmake --build build --config Release
# 产物：build/Release/tmcore.lib

# 构建动态库
cmake -B build-shared -DTMCORE_BUILD_SHARED=ON
cmake --build build-shared --config Release
# 产物：build-shared/Release/tmcore.dll（+ 导入库 tmcore.lib）
```

已验证环境：Windows 10 SDK 10.0.26100 / VS2022 (v143) / Qt 5.15.2 msvc2019_64，静态库、动态库、控制台及 Qt 示例均编译运行正常。

## 在 CMake 工程中使用

```cmake
add_subdirectory(tmcore)
target_link_libraries(your_app PRIVATE tmcore)
# iphlpapi / Pdh / powrprof 依赖会自动传播，无需手工链接
```

动态库模式下，可执行文件运行时需要能找到 `tmcore.dll`（放在 exe 同目录或加入 `PATH`）。

源码集成亦可：将 `src/` 加入目标，`include/` 加入头文件路径，链接 `iphlpapi Pdh powrprof`。

## 快速上手

注意：`NetworkMonitor` / `CpuUsage` / `GpuUsage` / `CpuFreq` / `DiskUsage` 实例需**保持长生命周期**（作为成员变量），跨调用维持内部状态；每次构造临时对象会导致 PDH 需重新预热、`GetSystemTimes` 模式结果失真。

```cpp
#include <tmcore/NetworkMonitor.h>
#include <tmcore/CpuUsage.h>
#include <tmcore/MemoryUsage.h>
#include <tmcore/GpuUsage.h>

class Monitor
{
    tmcore::NetworkMonitor m_net;   // 构造时枚举连接并自动选择
    tmcore::CpuUsage      m_cpu;
    tmcore::GpuUsage      m_gpu;

    void onTimer()  // QTimer / 定时器回调，建议间隔 >= 1 秒
    {
        const auto& s = m_net.Sample();   // 上/下行速度（字节/秒）与累计字节数
        int  cpu = m_cpu.GetUsage();
        auto mem = tmcore::MemoryUsage::Query();  // 静态函数，无需实例
        int  gpu{};
        m_gpu.GetUsage(gpu);             // 首次可能失败，属 PDH 预热正常现象
    }
};
```

## API 一览

| 类 | 主要接口 | 说明 |
|----|----------|------|
| `NetworkMonitor` | `RefreshConnections(all_interfaces)` | 刷新连接列表（`false` 仅网卡，`true` 含全部接口） |
| | `Select(index)` / `SelectAll()` / `AutoSelect()` | 选择监控目标（单连接 / 全部之和 / 流量最大者） |
| | `Sample()` → `TrafficSample` | 网速采样：`in_speed` / `out_speed`（字节/秒）、累计收发字节数 |
| | `GetIfRow([index,] row)` | 连接详情（`MIB_IFROW`：物理地址、MTU、链路速度、状态、收发字节/包数） |
| | `GetConnectionInfo(index, info)` | 连接信息（IP / 掩码 / 网关 / 描述） |
| | `Reinit()` | 睡眠唤醒、网络变化后重置 |
| `CpuUsage` | `GetUsage(use_cpu_time)` | `false`：PDH 计数器（失败自动回退 `GetSystemTimes`）；`true`：基于 CPU 时间 |
| `MemoryUsage` | `Query()` → `MemoryInfo` | 静态函数：利用率、物理内存总量/可用量 |
| `GpuUsage` | `GetUsage(usage)` | 0-100，取所有引擎类型最大值（同任务管理器），Win10+ |
| `CpuFreq` | `GetFreq(freq)` | 当前 CPU 频率（GHz） |
| `DiskUsage` | `GetUsage(index, usage)` / `GetDiskNames()` / `FindDiskIndex(name)` | 按物理磁盘序号取利用率；名称列表含 `_Total` 汇总实例 |

## 注意事项

- 静态库（默认）：直接链接，无 CRT 跨界问题
- 动态库：调用方需与库使用相同的编译器与 CRT（`/MD` 或 `/MT`）
- 显卡利用率、CPU 频率、硬盘利用率基于 PDH 计数器，**首次查询通常失败**，间隔 200ms 以上再次调用即可（`GpuUsage` / `CpuFreq` / `DiskUsage` 的首次失败是预期行为，不代表功能不可用）
- 睡眠唤醒、网络配置变化后可调用 `NetworkMonitor::Reinit()` 重置
- `dwInOctets` 为 32 位计数器，每秒超过 4GB 时该采样周期速度显示为 0（与 TrafficMonitor 原版行为一致）
- 连接切换（`Select` / `SelectAll` / `AutoSelect` / `RefreshConnections`）后的首次采样速度为 0，属正常行为

## 示例

- `examples/console_demo`：控制台验证程序，每 2 秒打印全部监控项
- `examples/qt_demo`：Qt5 QWidget 验证程序（连接下拉选择、每秒刷新、连接详情对话框）
