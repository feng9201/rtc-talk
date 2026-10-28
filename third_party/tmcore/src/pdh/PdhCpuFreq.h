#pragma once
// 移植自 TrafficMonitor/PdhHardwareQuery/CpuFreq.h
#include "PdhQuery.h"

class CPdhCpuFreq : public CPdhQuery
{
public:
    CPdhCpuFreq();

    // 调用此函数获取 CPU 频率。
    // 注意：第二次调用此函数开始才能获取到值，两次调用的时间不应过短，最好大于 200 毫秒
    bool GetCpuFreq(float& freq);

private:
    float max_cpu_freq = 0;
};
