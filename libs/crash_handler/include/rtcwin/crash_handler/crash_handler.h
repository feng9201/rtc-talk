#pragma once

#include <stddef.h>

#ifdef RTCWIN_CRASH_HANDLER_EXPORTS
#define RTCWIN_CRASH_API __declspec(dllexport)
#else
#define RTCWIN_CRASH_API __declspec(dllimport)
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct rtcwin_crash_handler rtcwin_crash_handler;

// Install once, before Qt/Engine startup; keep alive until after their teardown.
// NULL selects %LOCALAPPDATA%\rtc-talk-win\crash_dumps (TEMP fallback).
// Explicit paths must be absolute. Copies the path; returns NULL on failure.
RTCWIN_CRASH_API rtcwin_crash_handler* rtcwin_crash_handler_create(
    const wchar_t* dump_directory);
RTCWIN_CRASH_API void rtcwin_crash_handler_destroy(rtcwin_crash_handler* handler);

#ifdef __cplusplus
}
#endif
