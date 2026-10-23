// File: core/media/webrtc/video/webrtc_h264_factory.h
// Brief: 把 Media Foundation H.264 编解码器适配成 WebRTC 工厂。
//
// SDK 的 libwebrtc 以 rtc_use_h264=false 构建，内部没有任何 H.264 实现；
// 服务端 Router 只协商单层 H.264 Constrained Baseline（42e01f），因此这里
// 自建 VideoEncoderFactory / VideoDecoderFactory 注入 PeerConnectionFactory。
#ifndef TALK_MEDIA_WEBRTC_VIDEO_WEBRTC_H264_FACTORY_H_
#define TALK_MEDIA_WEBRTC_VIDEO_WEBRTC_H264_FACTORY_H_

#include <memory>

#include "api/video_codecs/video_decoder_factory.h"
#include "api/video_codecs/video_encoder_factory.h"
#include "api/video_codecs/sdp_video_format.h"

namespace talk {

// 与 server/src/media/codecs.ts 的 H264 参数逐字一致，服务端会校验。
const webrtc::SdpVideoFormat& H264ScreenFormat();

std::unique_ptr<webrtc::VideoEncoderFactory> CreateMfH264EncoderFactory();
std::unique_ptr<webrtc::VideoDecoderFactory> CreateMfH264DecoderFactory();

}  // namespace talk

#endif  // TALK_MEDIA_WEBRTC_VIDEO_WEBRTC_H264_FACTORY_H_
