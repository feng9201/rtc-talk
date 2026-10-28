// CPdhAdapterMemory 实现：按 LUID 聚合显存占用
#include "PdhAdapterMemory.h"

#include <cstdlib>

namespace
{

// 从 PDH 实例名解析 LUID：luid_0x00000000_0x0000DCA2_phys_0 -> 0x0000DCA2
uint64_t ParseLuid(const std::wstring& instance)
{
    const size_t pos = instance.find(L"luid_");
    if (pos == std::wstring::npos)
        return 0;

    wchar_t* end = nullptr;
    const unsigned long long hi = std::wcstoull(instance.c_str() + pos + 5, &end, 0);
    if (!end || *end != L'_')
        return 0;
    const unsigned long long lo = std::wcstoull(end + 1, nullptr, 0);
    return (hi << 32) | (lo & 0xFFFFFFFFull);
}

} // namespace

bool CPdhAdapterMemory::QueryLuid(uint64_t luid, uint64_t& usage)
{
    usage = 0;

    std::vector<CounterValueItem> items;
    if (!QueryValues(items))
        return false;

    for (const auto& item : items)
    {
        if (luid != 0 && ParseLuid(item.name) != luid)
            continue;
        if (item.value > 0.0)
            usage += static_cast<uint64_t>(item.value);
    }
    return true;
}
