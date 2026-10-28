#pragma once
#include "Export.h"

namespace tmcore
{

/**
 * @brief CPU 频率监控（PDH "% of Maximum Frequency"）
 *
 * 移植自 TrafficMonitor 的 CPdhCpuFreq。
 */
class TMCORE_API CpuFreq
{
public:
    CpuFreq();
    ~CpuFreq();
    CpuFreq(const CpuFreq&) = delete;
    CpuFreq& operator=(const CpuFreq&) = delete;

    /**
     * @brief 获取当前 CPU 频率（GHz）
     * @return true=成功, false=失败
     * @note  第二次调用开始才能获取到值，两次调用间隔应大于 200ms
     */
    bool GetFreq(float& freq);

private:
    class Impl;
    Impl* m_impl;
};

} // namespace tmcore
