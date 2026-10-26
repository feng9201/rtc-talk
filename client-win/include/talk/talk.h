#ifndef TALK_CLIENT_H
#define TALK_CLIENT_H
#include <stdint.h>
#include <stddef.h>
#if defined(_WIN32)
#if defined(TALK_BUILDING_LIBRARY)
#define TALK_API __declspec(dllexport)
#else
#define TALK_API __declspec(dllimport)
#endif
#elif defined(__GNUC__)
#define TALK_API __attribute__((visibility("default")))
#else
#define TALK_API
#endif
#ifdef __cplusplus
extern "C" {
#endif
#define TALK_ABI_VERSION 1u
typedef struct talk_handle talk_handle;
/* 事件在 core 控制线程串行调用；JSON 只在回调期间有效。
 * 回调可提交命令，但不得销毁句柄、阻塞或抛异常。 */
typedef void (*talk_event_fn)(void* userdata, const char* event,
                              const char* json);
/* 远端视频帧：BGRA，stride 为行字节数。在 webrtc 媒体线程回调；
 * bgra 与 mid 只在回调期间有效，不得阻塞或在该回调里销毁句柄。 */
typedef void (*talk_video_frame_fn)(void* userdata, const char* mid,
                                    const uint8_t* bgra, int width, int height,
                                    int stride, int64_t timestamp_us);
/* 本地摄像头预览帧：BGRA，在摄像头采集线程回调。
 * 用于入房前预览和本机画面；mid 固定为 "local:camera"。
 * bgra 只在回调期间有效，不得阻塞或在该回调里销毁句柄。 */
typedef void (*talk_local_video_frame_fn)(void* userdata, const char* mid,
                                          const uint8_t* bgra, int width,
                                          int height, int stride,
                                          int64_t timestamp_us);
typedef struct talk_options {
  uint32_t struct_size;
  uint32_t abi_version;
  talk_event_fn on_event;
  void* userdata;
  /* audio_backend: none（仅信令）、fake（无硬件）、wasapi（Windows Core Audio）。
   * 真实后端需用户明确选择；默认 none。
   * audio_source: mic（默认，麦克风）/ system（WASAPI loopback 采集默认播放设备）。
   * media_mode: auto（默认，两人且都支持时 P2P）/ sfu / p2p，作为入房策略提交。 */
  const char* config_json;
} talk_options;
TALK_API const char* talk_version(void);
TALK_API talk_handle* talk_create(const talk_options* options);
/* 返回操作 ID；0 表示输入无效。结果通过 result 事件返回。
 * 支持 connect/disconnect、room.join/leave、media.setMute、audio.devices/
 * volume/setSource、camera.devices、video.previewStart/Stop、media.stats、
 * session.ping/refreshIce/bye、auth.update。
 * 未实现的命令本地返回 501；不透传未知命令。
 * connect: {url,token}；auth.update: {token}，用于后续重连。
 * audio.setSource: {source: mic|system}，运行时切换采集源（系统音=WASAPI loopback）。
 * video.start / video.stop: 摄像头/屏幕共享启停（SFU 与 P2P 均支持）。
 * camera.devices: 枚举摄像头，返回 {cameras:[{index,name}]}；可在入房前调用。
 * video.previewStart: {camera_index}，启动本地摄像头预览；可在入房前调用。
 * video.previewStop: 停止本地预览；入房后摄像头共享期间不会停止采集。
 * room.join 使用协议参数。
 * 事件含 p2p.started / p2p.state（S3，服务端下发的 room.mediaMode 由核心自动应答）。 */
TALK_API uint64_t talk_command(talk_handle*, const char* method,
                              const char* data_json);
/* 可选：注册远端视频帧回调；传空回调取消。与 talk_options 解耦，
 * TALK_ABI_VERSION 保持 1，旧调用方无需改动。 */
TALK_API void talk_set_video_frame_callback(talk_handle*,
                                            talk_video_frame_fn callback,
                                            void* userdata);
/* 可选：注册本地摄像头预览回调；传空回调取消。
 * 与远端视频回调独立，仍保持 TALK_ABI_VERSION=1。 */
TALK_API void talk_set_local_video_frame_callback(
    talk_handle*, talk_local_video_frame_fn callback, void* userdata);
/* 调用方须停止所有 API 调用后在非回调线程销毁；返回后不再回调。 */
TALK_API void talk_destroy(talk_handle*);
#ifdef __cplusplus
}
#endif
#endif
