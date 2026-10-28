#pragma once
// 内部工具函数（不导出），替代 TrafficMonitor 中 CCommon 的相关实现
#include <cstdint>
#include <string>
#include <windows.h>

namespace tmhelper
{

// ANSI/UTF-8 字符串 -> 宽字符字符串
std::wstring StrToUnicode(const char* str, bool utf8 = false);

// 宽字符字符串 -> ANSI/UTF-8 字符串
std::string UnicodeToStr(const wchar_t* wstr, bool utf8 = false);

// FILETIME 差值（time2 - time1）
__int64 CompareFileTime2(FILETIME time1, FILETIME time2);

// 计算两个字符串的相似度（Levenshtein 距离），用于网卡名称模糊匹配
double StringSimilarDegree_LD(const std::string& src, const std::string& match);

// 判断当前系统是否 Windows 10 及以上（RtlGetVersion，不受 manifest 影响）
bool IsWindows10OrGreater();

// 当前时间戳（毫秒）
uint64_t GetCurrentTimeMs();

} // namespace tmhelper
