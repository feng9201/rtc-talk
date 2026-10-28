// 内部工具函数实现，移植自 TrafficMonitor/Common.cpp
#include "StringHelper.h"
#include <vector>

namespace tmhelper
{

std::wstring StrToUnicode(const char* str, bool utf8)
{
    std::wstring result;
    int code_page = utf8 ? CP_UTF8 : CP_ACP;
    int n_len = MultiByteToWideChar(code_page, 0, str, -1, NULL, 0);
    if (n_len > 0)
    {
        result.resize(n_len - 1);
        if (n_len > 1)
            MultiByteToWideChar(code_page, 0, str, -1, &result[0], n_len);
    }
    return result;
}

std::string UnicodeToStr(const wchar_t* wstr, bool utf8)
{
    std::string result;
    int code_page = utf8 ? CP_UTF8 : CP_ACP;
    int n_len = WideCharToMultiByte(code_page, 0, wstr, -1, NULL, 0, NULL, NULL);
    if (n_len > 0)
    {
        result.resize(n_len - 1);
        if (n_len > 1)
            WideCharToMultiByte(code_page, 0, wstr, -1, &result[0], n_len, NULL, NULL);
    }
    return result;
}

__int64 CompareFileTime2(FILETIME time1, FILETIME time2)
{
    __int64 a = static_cast<__int64>(time1.dwHighDateTime) << 32 | time1.dwLowDateTime;
    __int64 b = static_cast<__int64>(time2.dwHighDateTime) << 32 | time2.dwLowDateTime;
    return b - a;
}

double StringSimilarDegree_LD(const std::string& src, const std::string& match)
{
    int n = static_cast<int>(src.size());
    int m = static_cast<int>(match.size());
    if (n == 0 || m == 0)
        return 0.0;

    std::vector<std::vector<int>> d(n + 1, std::vector<int>(m + 1));
    for (int i = 0; i <= n; i++)
        d[i][0] = i;
    for (int j = 0; j <= m; j++)
        d[0][j] = j;

    for (int i = 1; i <= n; i++)
    {
        for (int j = 1; j <= m; j++)
        {
            int cost = (match[j - 1] == src[i - 1] ? 0 : 1);
            d[i][j] = (std::min)((std::min)(d[i - 1][j] + 1, d[i][j - 1] + 1), d[i - 1][j - 1] + cost);
        }
    }

    double ds = 1 - static_cast<double>(d[n][m]) / (std::max)(src.size(), match.size());
    return ds;
}

bool IsWindows10OrGreater()
{
    typedef LONG(WINAPI * RtlGetVersionPtr)(PRTL_OSVERSIONINFOW);
    RTL_OSVERSIONINFOW info{};
    info.dwOSVersionInfoSize = sizeof(info);
    HMODULE h_ntdll = ::GetModuleHandleW(L"ntdll.dll");
    if (h_ntdll != NULL)
    {
        auto fn = reinterpret_cast<RtlGetVersionPtr>(::GetProcAddress(h_ntdll, "RtlGetVersion"));
        if (fn != nullptr && fn(&info) == 0)
            return info.dwMajorVersion >= 10;
    }
    return false;
}

uint64_t GetCurrentTimeMs()
{
    return static_cast<uint64_t>(::GetTickCount64());
}

} // namespace tmhelper
