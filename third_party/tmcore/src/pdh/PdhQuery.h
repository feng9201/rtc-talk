#pragma once
// 移植自 TrafficMonitor/PdhHardwareQuery/PdhQuery.h
// 改动：CString -> std::wstring，显式使用 W 版本 PDH API（不依赖 UNICODE 宏）
#include <Pdh.h>
#include <PdhMsg.h>
#include <string>
#include <vector>

class CPdhQuery
{
public:
    CPdhQuery(const wchar_t* full_counter_path);
    virtual ~CPdhQuery();

protected:
    bool Initialize();
    bool QueryValue(double& value);

    struct CounterValueItem
    {
        std::wstring name;
        double value{};
    };
    bool QueryValues(std::vector<CounterValueItem>& values);

protected:
    HQUERY query = nullptr;
    HCOUNTER counter = nullptr;
    bool isInitialized = false;
    std::wstring fullCounterPath;
};
