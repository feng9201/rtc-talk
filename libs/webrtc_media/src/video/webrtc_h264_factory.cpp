// File: core/webrtc_h264_factory.cpp
// Brief: 基于 Media Foundation 的 H.264 编解码器工厂实现。
#include "webrtc_h264_factory.h"

#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "api/environment/environment.h"
#include "api/video/encoded_image.h"
#include "api/video/i420_buffer.h"
#include "api/video/video_frame.h"
#include "api/video/video_frame_buffer.h"
#include "api/video_codecs/video_codec.h"
#include "api/video_codecs/video_encoder.h"
#include "mf_h264_codec.h"
#include "modules/video_coding/codecs/h264/include/h264_globals.h"
#include "modules/video_coding/include/video_codec_interface.h"
#include "modules/video_coding/include/video_error_codes.h"
#include "rtc_base/logging.h"
#include "third_party/libyuv/include/libyuv/convert.h"
#include "third_party/libyuv/include/libyuv/scale.h"

namespace rtcwin::webrtc_media {
namespace {

constexpr int kDefaultFps = 15;
constexpr int kMaxFps = 30;
constexpr int kDefaultBitrateBps = 1200000;

// MF 的 H.264 MFT 只接受 NV12，要求宽高为偶数。
int AlignEven(int value) {
  return value & ~1;
}

int ClampFps(int fps) {
  if (fps <= 0) {
    return kDefaultFps;
  }
  return fps > kMaxFps ? kMaxFps : fps;
}

int ResolveBitrate(const webrtc::VideoCodec& codec) {
  int bitrate = static_cast<int>(codec.startBitrate) * 1000;
  const int max_bitrate = static_cast<int>(codec.maxBitrate) * 1000;
  if (max_bitrate > 0 && (bitrate <= 0 || bitrate > max_bitrate)) {
    bitrate = max_bitrate;
  }
  if (bitrate <= 0) {
    bitrate = kDefaultBitrateBps;
  }
  return bitrate;
}

// Frame 的缓冲不一定是 I420（桌面采集给的就是 I420），必要时缩放并转 NV12。
class Nv12Scratch {
 public:
  bool Fill(const webrtc::VideoFrame& frame, int width, int height) {
    webrtc::scoped_refptr<webrtc::VideoFrameBuffer> buffer =
        frame.video_frame_buffer();
    if (!buffer) {
      return false;
    }
    webrtc::scoped_refptr<webrtc::I420BufferInterface> i420 = buffer->ToI420();
    if (!i420) {
      return false;
    }

    webrtc::scoped_refptr<webrtc::I420Buffer> scaled;
    const webrtc::I420BufferInterface* source = i420.get();
    if (i420->width() != width || i420->height() != height) {
      // 编码器尺寸与输入不一致时按最近邻缩放，避免整帧编码失败。
      scaled = webrtc::I420Buffer::Create(width, height);
      libyuv::I420Scale(i420->DataY(), i420->StrideY(), i420->DataU(),
                        i420->StrideU(), i420->DataV(), i420->StrideV(),
                        i420->width(), i420->height(), scaled->MutableDataY(),
                        scaled->StrideY(), scaled->MutableDataU(),
                        scaled->StrideU(), scaled->MutableDataV(),
                        scaled->StrideV(), width, height,
                        libyuv::kFilterBilinear);
      source = scaled.get();
    }

    const size_t needed = static_cast<size_t>(width) * height * 3 / 2;
    if (nv12_.size() < needed) {
      nv12_.resize(needed);
    }
    uint8_t* dst_y = nv12_.data();
    uint8_t* dst_uv = dst_y + static_cast<size_t>(width) * height;
    // NV12 的交错 UV 平面步长必须等于 Y 平面宽度。
    libyuv::I420ToNV12(source->DataY(), source->StrideY(), source->DataU(),
                       source->StrideU(), source->DataV(), source->StrideV(),
                       dst_y, width, dst_uv, width, width, height);
    return true;
  }

  const uint8_t* data() const { return nv12_.data(); }

 private:
  std::vector<uint8_t> nv12_;
};

class MfVideoEncoder : public webrtc::VideoEncoder {
 public:
  MfVideoEncoder() = default;
  ~MfVideoEncoder() override { Release(); }

  MfVideoEncoder(const MfVideoEncoder&) = delete;
  MfVideoEncoder& operator=(const MfVideoEncoder&) = delete;

  int InitEncode(const webrtc::VideoCodec* codec,
                 const webrtc::VideoEncoder::Settings& /*settings*/) override {
    if (codec == nullptr) {
      return WEBRTC_VIDEO_CODEC_ERR_PARAMETER;
    }
    const int width = AlignEven(codec->width);
    const int height = AlignEven(codec->height);
    if (width < 16 || height < 16) {
      return WEBRTC_VIDEO_CODEC_ERR_PARAMETER;
    }
    const int fps = ClampFps(static_cast<int>(codec->maxFramerate));
    const int bitrate = ResolveBitrate(*codec);
    if (!encoder_.Open(width, height, fps, bitrate)) {
      RTC_LOG(LS_WARNING) << "MF H.264 编码器初始化失败 size=" << width << "x"
                          << height << " fps=" << fps << " bitrate=" << bitrate;
      return WEBRTC_VIDEO_CODEC_ERROR;
    }
    width_ = width;
    height_ = height;
    fps_ = fps;
    bitrate_ = bitrate;
    RTC_LOG(LS_INFO) << "MF H.264 编码器已启动 size=" << width << "x" << height
                     << " fps=" << fps << " bitrate=" << bitrate
                     << " mode=" << static_cast<int>(codec->mode);
    return WEBRTC_VIDEO_CODEC_OK;
  }

  int32_t RegisterEncodeCompleteCallback(
      webrtc::EncodedImageCallback* callback) override {
    callback_ = callback;
    return WEBRTC_VIDEO_CODEC_OK;
  }

  int32_t Release() override {
    encoder_.Close();
    callback_ = nullptr;
    width_ = 0;
    height_ = 0;
    fps_ = 0;
    bitrate_ = 0;
    return WEBRTC_VIDEO_CODEC_OK;
  }

  int32_t Encode(const webrtc::VideoFrame& frame,
                 const std::vector<webrtc::VideoFrameType>* frame_types)
      override {
    if (!encoder_.opened() || callback_ == nullptr) {
      return WEBRTC_VIDEO_CODEC_UNINITIALIZED;
    }
    bool force_key = false;
    if (frame_types != nullptr) {
      for (webrtc::VideoFrameType type : *frame_types) {
        if (type == webrtc::VideoFrameType::kVideoFrameKey) {
          force_key = true;
        }
      }
    }
    if (!scratch_.Fill(frame, width_, height_)) {
      return WEBRTC_VIDEO_CODEC_ERR_PARAMETER;
    }

    std::string payload;
    bool keyframe = false;
    if (!encoder_.Encode(scratch_.data(), width_, frame.timestamp_us(),
                         force_key, &payload, &keyframe)) {
      return WEBRTC_VIDEO_CODEC_ERROR;
    }
    if (payload.empty()) {
      // MFT 偶尔延迟一拍输出：本帧不回调，下一次 Encode 会补上。
      return WEBRTC_VIDEO_CODEC_OK;
    }

    webrtc::scoped_refptr<webrtc::EncodedImageBuffer> encoded =
        webrtc::EncodedImageBuffer::Create(payload.size());
    if (!encoded) {
      return WEBRTC_VIDEO_CODEC_MEMORY;
    }
    std::memcpy(encoded->data(), payload.data(), payload.size());

    webrtc::EncodedImage image;
    image.SetEncodedData(encoded);
    image._encodedWidth = static_cast<uint32_t>(width_);
    image._encodedHeight = static_cast<uint32_t>(height_);
    image.SetRtpTimestamp(frame.rtp_timestamp());
    image.capture_time_ms_ = frame.timestamp_us() / 1000;
    image.ntp_time_ms_ = frame.ntp_time_ms();
    image.rotation_ = frame.rotation();
    image.SetFrameType(keyframe ? webrtc::VideoFrameType::kVideoFrameKey
                                : webrtc::VideoFrameType::kVideoFrameDelta);

    webrtc::CodecSpecificInfo info;
    info.codecType = webrtc::kVideoCodecH264;
    info.codecSpecific.H264.packetization_mode =
        webrtc::H264PacketizationMode::NonInterleaved;
    info.codecSpecific.H264.temporal_idx = 0;
    info.codecSpecific.H264.base_layer_sync = false;
    info.codecSpecific.H264.idr_frame = keyframe;

    const webrtc::EncodedImageCallback::Result result =
        callback_->OnEncodedImage(image, &info);
    if (result.error != webrtc::EncodedImageCallback::Result::OK) {
      return WEBRTC_VIDEO_CODEC_ERROR;
    }
    return WEBRTC_VIDEO_CODEC_OK;
  }

  void SetRates(const RateControlParameters& parameters) override {
    const int bitrate = static_cast<int>(parameters.bitrate.get_sum_bps());
    if (bitrate > 0 && bitrate != bitrate_) {
      bitrate_ = bitrate;
      encoder_.SetBitrate(bitrate);
    }
  }

  webrtc::VideoEncoder::EncoderInfo GetEncoderInfo() const override {
    EncoderInfo info;
    info.implementation_name = "MF H264";
    info.is_hardware_accelerated = false;
    info.supports_native_handle = false;
    info.has_trusted_rate_controller = false;
    info.supports_simulcast = false;
    // NV12 要求偶数宽高；让 WebRTC 在送帧前就对齐。
    info.requested_resolution_alignment = 2;
    info.apply_alignment_to_all_simulcast_layers = false;
    // 单空间层单时间层：服务端只协商一层，避免 SVC 期望多帧输出。
    for (auto& allocation : info.fps_allocation) {
      allocation.clear();
    }
    info.fps_allocation[0].push_back(EncoderInfo::kMaxFramerateFraction);
    return info;
  }

 private:
  MfH264Encoder encoder_;
  Nv12Scratch scratch_;
  webrtc::EncodedImageCallback* callback_ = nullptr;
  int width_ = 0;
  int height_ = 0;
  int fps_ = 0;
  int bitrate_ = 0;
};

class MfVideoDecoder : public webrtc::VideoDecoder {
 public:
  bool Configure(const Settings& /*settings*/) override {
    decoder_.Close();
    configured_ = true;
    return true;
  }

  int32_t RegisterDecodeCompleteCallback(
      webrtc::DecodedImageCallback* callback) override {
    callback_ = callback;
    return WEBRTC_VIDEO_CODEC_OK;
  }

  int32_t Release() override {
    decoder_.Close();
    callback_ = nullptr;
    configured_ = false;
    timestamp_us_ = 0;
    return WEBRTC_VIDEO_CODEC_OK;
  }

  int32_t Decode(const webrtc::EncodedImage& input, bool /*missing_frames*/,
                 int64_t /*render_time_ms*/) override {
    if (!configured_ || callback_ == nullptr) {
      return WEBRTC_VIDEO_CODEC_UNINITIALIZED;
    }
    if (input.data() == nullptr || input.size() == 0) {
      return WEBRTC_VIDEO_CODEC_ERR_PARAMETER;
    }
    // 解码器内部只要求时间戳单调，这里用本地计数避免上游时钟回退。
    timestamp_us_ += 1000000 / kDefaultFps;
    std::vector<uint8_t> i420;
    int width = 0;
    int height = 0;
    if (!decoder_.Decode(input.data(), input.size(), timestamp_us_, &i420,
                         &width, &height)) {
      // 还没收到带 SPS 的关键帧，等下一帧；不算解码错误。
      return WEBRTC_VIDEO_CODEC_OK;
    }

    webrtc::scoped_refptr<webrtc::I420Buffer> buffer =
        webrtc::I420Buffer::Create(width, height);
    if (!buffer) {
      return WEBRTC_VIDEO_CODEC_MEMORY;
    }
    const size_t plane = static_cast<size_t>(width) * height;
    const uint8_t* src_y = i420.data();
    const uint8_t* src_u = src_y + plane;
    const uint8_t* src_v = src_u + plane / 4;
    libyuv::I420Copy(src_y, width, src_u, width / 2, src_v, width / 2,
                     buffer->MutableDataY(), buffer->StrideY(),
                     buffer->MutableDataU(), buffer->StrideU(),
                     buffer->MutableDataV(), buffer->StrideV(), width, height);

    webrtc::VideoFrame frame =
        webrtc::VideoFrame::Builder()
            .set_video_frame_buffer(buffer)
            .set_timestamp_rtp(input.RtpTimestamp())
            .set_ntp_time_ms(input.ntp_time_ms_)
            .build();
    callback_->Decoded(frame);
    return WEBRTC_VIDEO_CODEC_OK;
  }

  webrtc::VideoDecoder::DecoderInfo GetDecoderInfo() const override {
    DecoderInfo info;
    info.implementation_name = "MF H264";
    info.is_hardware_accelerated = false;
    return info;
  }

  const char* ImplementationName() const override { return "MF H264"; }

 private:
  MfH264Decoder decoder_;
  webrtc::DecodedImageCallback* callback_ = nullptr;
  bool configured_ = false;
  int64_t timestamp_us_ = 0;
};

class MfH264EncoderFactory : public webrtc::VideoEncoderFactory {
 public:
  std::vector<webrtc::SdpVideoFormat> GetSupportedFormats() const override {
    return {H264SfuFormat()};
  }

  std::unique_ptr<webrtc::VideoEncoder> Create(
      const webrtc::Environment& /*env*/,
      const webrtc::SdpVideoFormat& /*format*/) override {
    return std::make_unique<MfVideoEncoder>();
  }
};

class MfH264DecoderFactory : public webrtc::VideoDecoderFactory {
 public:
  std::vector<webrtc::SdpVideoFormat> GetSupportedFormats() const override {
    return {H264SfuFormat()};
  }

  std::unique_ptr<webrtc::VideoDecoder> Create(
      const webrtc::Environment& /*env*/,
      const webrtc::SdpVideoFormat& /*format*/) override {
    return std::make_unique<MfVideoDecoder>();
  }
};

}  // namespace

const webrtc::SdpVideoFormat& H264SfuFormat() {
  // 与 server/src/media/codecs.ts 的 codecOptions 保持一致：服务端会拒绝
  // profile-level-id 不是 42e01f 的 answer。
  static const webrtc::SdpVideoFormat format(
      "H264", {{"profile-level-id", "42e01f"},
               {"packetization-mode", "1"},
               {"level-asymmetry-allowed", "1"}});
  return format;
}

std::unique_ptr<webrtc::VideoEncoderFactory> CreateMfH264EncoderFactory() {
  return std::make_unique<MfH264EncoderFactory>();
}

std::unique_ptr<webrtc::VideoDecoderFactory> CreateMfH264DecoderFactory() {
  return std::make_unique<MfH264DecoderFactory>();
}

}  // namespace rtcwin::webrtc_media
