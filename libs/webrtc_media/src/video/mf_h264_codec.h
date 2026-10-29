// File: core/mf_h264_codec.h
// Brief: 基于 Media Foundation 的 H.264 编解码器（单层，无 B 帧，低延迟）。
//
// SDK 里的 libwebrtc 未内置 H.264 编解码器（rtc_use_h264=false），需要外部注入
// VideoEncoderFactory / VideoDecoderFactory。这里用 Windows 自带的 H.264 MFT，
// 不引入 FFmpeg/libx264，保持 /MT 与单 DLL 分发。
#ifndef RTCWIN_WEBRTC_MEDIA_MF_H264_CODEC_H_
#define RTCWIN_WEBRTC_MEDIA_MF_H264_CODEC_H_

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace rtcwin::webrtc_media {

// 编码器：NV12 输入，输出 Annex-B 访问单元（关键帧自带 SPS/PPS）。
class MfH264Encoder {
 public:
  MfH264Encoder();
  ~MfH264Encoder();

  MfH264Encoder(const MfH264Encoder&) = delete;
  MfH264Encoder& operator=(const MfH264Encoder&) = delete;

  // fps 用于码控与帧时长；画面尺寸变化需重新 Open。
  bool Open(int width, int height, int fps, int bitrate_bps);
  void Close();
  bool opened() const;
  int width() const;
  int height() const;

  // stride 为 NV12 每行字节数（Y/UV 共用）。force_key 请求 IDR。
  // 帧被内部丢弃（编码器暂时不收）时返回 true 但 out 为空。
  bool Encode(const uint8_t* nv12, int stride, int64_t timestamp_us,
              bool force_key, std::string* out, bool* keyframe);
  bool SetBitrate(int bitrate_bps);

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

// 解码器：Annex-B 输入，输出紧凑 I420。尺寸从 SPS 解析，变化时自动重建。
class MfH264Decoder {
 public:
  MfH264Decoder();
  ~MfH264Decoder();

  MfH264Decoder(const MfH264Decoder&) = delete;
  MfH264Decoder& operator=(const MfH264Decoder&) = delete;

  void Close();
  // 解出至少一帧时返回 true；无 SPS 且尚未配置时返回 false（等关键帧）。
  bool Decode(const uint8_t* data, size_t size, int64_t timestamp_us,
              std::vector<uint8_t>* i420, int* width, int* height);

 private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

// Media Foundation 启动（进程内只做一次）。
bool EnsureMediaFoundation();

}  // namespace rtcwin::webrtc_media

#endif  // RTCWIN_WEBRTC_MEDIA_MF_H264_CODEC_H_
