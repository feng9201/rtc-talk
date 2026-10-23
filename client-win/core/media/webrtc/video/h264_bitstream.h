// File: core/media/webrtc/video/h264_bitstream.h
// Brief: H.264 码流工具：AVCC/Annex-B 归一、参数集、关键帧与 SPS 尺寸解析。
//
// Media Foundation 的 H.264 编码器输出可能是 AVCC（4 字节长度前缀），而 RTP
// 打包需要 Annex-B（起始码）；解码器又需要先知道 SPS 给出的画面尺寸才能设置
// NV12 输出类型。这里集中处理这些纯码流问题，不依赖 MF 与 WebRTC。
#ifndef TALK_MEDIA_WEBRTC_VIDEO_H264_BITSTREAM_H_
#define TALK_MEDIA_WEBRTC_VIDEO_H264_BITSTREAM_H_

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace talk {

// 统一成 Annex-B：已是 Annex-B 时原样复制，AVCC 时逐 NAL 改写起始码。
// 无法识别的输入返回空，由调用方按坏帧丢弃。
std::string ToAnnexB(const uint8_t* data, size_t size);

// 收集码流里的 SPS(7)/PPS(8)，输出带 4 字节起始码的 NAL；找不到的保持为空。
void CollectParameterSets(const std::string& annexb, std::string* sps,
                          std::string* pps);

// 是否包含指定 NAL 类型（5=IDR，7=SPS，8=PPS）。
bool ContainsNalType(const std::string& annexb, uint8_t nal_type);

// 从 Annex-B 中取第一个 SPS 解析显示尺寸；失败返回 false。
bool ParseSpsSize(const std::string& annexb, int* width, int* height);

}  // namespace talk

#endif  // TALK_MEDIA_WEBRTC_VIDEO_H264_BITSTREAM_H_
