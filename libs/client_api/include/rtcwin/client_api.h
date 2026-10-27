#pragma once

#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32)
#if defined(RTCWIN_ENGINE_EXPORTS)
#define RTCWIN_API __declspec(dllexport)
#else
#define RTCWIN_API __declspec(dllimport)
#endif
#else
#define RTCWIN_API
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define RTCWIN_ABI_VERSION 1u

typedef struct rtcwin_handle rtcwin_handle;

typedef void (*rtcwin_event_fn)(void* userdata, const char* method,
                                const char* json);

typedef struct rtcwin_frame_view {
  const char* mid;
  const uint8_t* i420_y;
  const uint8_t* i420_u;
  const uint8_t* i420_v;
  int32_t width;
  int32_t height;
  int32_t stride_y;
  int32_t stride_u;
  int32_t stride_v;
  int64_t timestamp_us;
  uint8_t source;
} rtcwin_frame_view;

typedef void (*rtcwin_frame_fn)(void* userdata,
                                const rtcwin_frame_view* frame);

typedef struct rtcwin_options {
  uint32_t struct_size;
  uint32_t abi_version;
  void* userdata;
  rtcwin_event_fn on_event;
  rtcwin_frame_fn on_remote_frame;
  rtcwin_frame_fn on_local_frame;
} rtcwin_options;

RTCWIN_API rtcwin_handle* rtcwin_create(const rtcwin_options* options);
RTCWIN_API void rtcwin_destroy(rtcwin_handle* handle);
RTCWIN_API uint64_t rtcwin_submit(rtcwin_handle* handle, const char* method,
                                  const char* json, int32_t timeout_ms);
RTCWIN_API const char* rtcwin_version(void);

#ifdef __cplusplus
}
#endif
