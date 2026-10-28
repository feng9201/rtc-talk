// 移植自 TrafficMonitor/PdhHardwareQuery/PdhQuery.cpp
// 改动：CString::GetString() 移除，显式使用 W 版本 PDH API
#include "PdhQuery.h"

CPdhQuery::CPdhQuery(const wchar_t* full_counter_path)
    : fullCounterPath(full_counter_path)
{
    Initialize();
}

CPdhQuery::~CPdhQuery()
{
    // 关闭查询
    PdhCloseQuery(query);
}

bool CPdhQuery::Initialize()
{
    if (isInitialized)
        return true;

    PDH_STATUS status;
    // 打开查询
    status = PdhOpenQueryW(NULL, NULL, &query);
    if (status != ERROR_SUCCESS)
        return false;

    // 添加计数器
    status = PdhAddCounterW(query, fullCounterPath.c_str(), NULL, &counter);
    // 当 PdhAddCounterW 失败时使用 PdhAddEnglishCounterW 再试一次
    if (status != ERROR_SUCCESS)
    {
        status = PdhAddEnglishCounterW(query, fullCounterPath.c_str(), NULL, &counter);
        if (status != ERROR_SUCCESS)
        {
            PdhCloseQuery(query);
            query = nullptr;
            return false;
        }
    }

    // 初始化计数器数据
    PdhCollectQueryData(query);
    isInitialized = true;
    return true;
}

bool CPdhQuery::QueryValue(double& value)
{
    if (!isInitialized)
        return false;

    // 收集数据
    PdhCollectQueryData(query);
    PDH_FMT_COUNTERVALUE pdhValue;
    DWORD dwValue;
    PDH_STATUS status = PdhGetFormattedCounterValue(counter, PDH_FMT_DOUBLE, &dwValue, &pdhValue);
    if (status != ERROR_SUCCESS)
    {
        return false;
    }
    value = pdhValue.doubleValue;
    return true;
}

bool CPdhQuery::QueryValues(std::vector<CounterValueItem>& values)
{
    values.clear();
    if (!isInitialized)
        return false;

    // 收集数据
    PdhCollectQueryData(query);
    DWORD dwBufferSize = 0;  // pItems 缓冲区大小
    DWORD dwItemCount = 0;  // pItems 中的项数
    PDH_FMT_COUNTERVALUE_ITEM_W* pItems = NULL;
    PDH_STATUS status = PdhGetFormattedCounterArrayW(counter, PDH_FMT_DOUBLE, &dwBufferSize, &dwItemCount, pItems);
    if (PDH_MORE_DATA == status)
    {
        pItems = (PDH_FMT_COUNTERVALUE_ITEM_W*)malloc(dwBufferSize);
        if (pItems)
        {
            status = PdhGetFormattedCounterArrayW(counter, PDH_FMT_DOUBLE, &dwBufferSize, &dwItemCount, pItems);
            if (ERROR_SUCCESS == status)
            {
                for (DWORD i = 0; i < dwItemCount; i++)
                {
                    CounterValueItem value_item;
                    value_item.name = pItems[i].szName;
                    value_item.value = pItems[i].FmtValue.doubleValue;
                    values.push_back(value_item);
                }
            }
            else
            {
                free(pItems);
                return false;
            }

            free(pItems);
            pItems = NULL;
            dwBufferSize = dwItemCount = 0;
        }
        else
        {
            return false;
        }
    }
    else
    {
        return false;
    }

    return true;
}
