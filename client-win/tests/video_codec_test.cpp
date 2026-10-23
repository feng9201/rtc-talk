// File: tests/video_codec_test.cpp
// Brief: 校验 MF H.264 编解码回环与 Annex-B 码流工具。
//
// 该测试与 libwebrtc 共用 /MT 静态库，但只用 printf 汇报结果：
// 异常对象跨 _HAS_EXCEPTIONS=0 的编译单元容易失真，这里改为返回值判定。
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "media/webrtc/video/h264_bitstream.h"
#include "media/webrtc/video/mf_h264_codec.h"

namespace {

constexpr int kWidth = 640;
constexpr int kHeight = 360;
constexpr int kFps = 15;
constexpr int kBitrateBps = 1200000;
constexpr int kMaxFrames = 60;

bool Check(bool ok, const char* message) {
  if (!ok) {
    std::printf("FAIL: %s\n", message);
  }
  return ok;
}

// 生成带滚动竖条的 NV12 测试图：纹理足够让编码器产生真实宏块数据。
void FillNv12(std::vector<uint8_t>* nv12, int frame) {
  const size_t luma = static_cast<size_t>(kWidth) * kHeight;
  nv12->resize(luma * 3 / 2);
  uint8_t* y = nv12->data();
  for (int row = 0; row < kHeight; ++row) {
    for (int col = 0; col < kWidth; ++col) {
      const int stripe = ((col + frame * 6) / 32) % 2;
      y[static_cast<size_t>(row) * kWidth + col] =
          static_cast<uint8_t>(stripe != 0 ? 210 : 30);
    }
  }
  uint8_t* uv = y + luma;
  for (int row = 0; row < kHeight / 2; ++row) {
    for (int col = 0; col < kWidth / 2; ++col) {
      uv[static_cast<size_t>(row) * kWidth + col * 2] = 128;
      uv[static_cast<size_t>(row) * kWidth + col * 2 + 1] = 128;
    }
  }
}

// 按起始码拆分 Annex-B，返回各 NAL 负载（不含起始码）。
std::vector<std::string> SplitAnnexB(const std::string& data) {
  std::vector<std::string> nals;
  size_t pos = 0;
  size_t nal_start = std::string::npos;
  while (pos + 2 < data.size()) {
    if (static_cast<uint8_t>(data[pos]) == 0 &&
        static_cast<uint8_t>(data[pos + 1]) == 0 &&
        static_cast<uint8_t>(data[pos + 2]) == 1) {
      if (nal_start != std::string::npos) {
        size_t end = pos;
        // 4 字节起始码的前导 0 属于下一个起始码。
        while (end > nal_start && static_cast<uint8_t>(data[end - 1]) == 0) {
          --end;
        }
        if (end > nal_start) {
          nals.push_back(data.substr(nal_start, end - nal_start));
        }
      }
      pos += 3;
      nal_start = pos;
      continue;
    }
    ++pos;
  }
  if (nal_start != std::string::npos && nal_start < data.size()) {
    nals.push_back(data.substr(nal_start));
  }
  return nals;
}

// 把 Annex-B 改写成 AVCC（4 字节大端长度前缀），用于验证 ToAnnexB。
std::string AnnexBToAvcc(const std::string& annexb) {
  std::string out;
  for (const std::string& nal : SplitAnnexB(annexb)) {
    const uint32_t size = static_cast<uint32_t>(nal.size());
    out.push_back(static_cast<char>((size >> 24) & 0xFF));
    out.push_back(static_cast<char>((size >> 16) & 0xFF));
    out.push_back(static_cast<char>((size >> 8) & 0xFF));
    out.push_back(static_cast<char>(size & 0xFF));
    out.append(nal);
  }
  return out;
}

bool CheckBitstreamTools() {
  // 两个 NAL：SPS（0x67）+ 非 IDR 切片（0x41）。
  // 尾部不留 0x00，否则会被当成 trailing_zero_8bits 剥离，长度不好对账。
  std::string annexb("\x00\x00\x00\x01\x67\x42\x00\x1f", 8);
  annexb.append("\x00\x00\x00\x01\x41\x9a\x03", 7);
  if (!Check(talk::ContainsNalType(annexb, 7), "annexb should contain SPS")) {
    return false;
  }
  if (!Check(!talk::ContainsNalType(annexb, 5),
             "annexb should not contain IDR")) {
    return false;
  }

  std::string sps;
  std::string pps;
  talk::CollectParameterSets(annexb, &sps, &pps);
  if (!Check(!sps.empty(), "SPS should be collected")) {
    return false;
  }
  if (!Check(pps.empty(), "PPS should stay empty")) {
    return false;
  }

  // AVCC 用 4 字节长度前缀替换 4 字节起始码，总长度不变。
  const std::string avcc = AnnexBToAvcc(annexb);
  if (!Check(avcc.size() == annexb.size(),
             "AVCC should carry length prefixes")) {
    return false;
  }
  if (!Check(static_cast<uint8_t>(avcc[3]) == 4 &&
                 static_cast<uint8_t>(avcc[4]) == 0x67,
             "first NAL should be length-prefixed SPS")) {
    return false;
  }
  const std::string normalized = talk::ToAnnexB(
      reinterpret_cast<const uint8_t*>(avcc.data()), avcc.size());
  if (!Check(normalized == annexb, "AVCC to annexb content mismatch")) {
    return false;
  }

  const std::string passthrough = talk::ToAnnexB(
      reinterpret_cast<const uint8_t*>(annexb.data()), annexb.size());
  return Check(passthrough == annexb, "annexb should pass through");
}

bool CheckCodecRoundTrip() {
  if (!Check(talk::EnsureMediaFoundation(), "MFStartup failed")) {
    return false;
  }

  talk::MfH264Encoder encoder;
  if (!Check(encoder.Open(kWidth, kHeight, kFps, kBitrateBps),
             "encoder init failed (H.264 MFT missing?)")) {
    return false;
  }

  std::vector<std::string> frames;
  std::string keyframe;
  std::vector<uint8_t> nv12;
  for (int i = 0; i < kMaxFrames; ++i) {
    FillNv12(&nv12, i);
    std::string out;
    bool key = false;
    if (!Check(encoder.Encode(nv12.data(), kWidth,
                              static_cast<int64_t>(i) * 1000000 / kFps, i == 0,
                              &out, &key),
               "encode failed")) {
      return false;
    }
    if (out.empty()) {
      continue;
    }
    frames.push_back(out);
    if (key && keyframe.empty()) {
      keyframe = out;
    }
    if (!keyframe.empty() && frames.size() >= 2) {
      break;
    }
  }
  if (!Check(!frames.empty(), "encoder produced no bitstream")) {
    return false;
  }
  if (!Check(!keyframe.empty(), "no keyframe produced")) {
    return false;
  }
  if (!Check(talk::ContainsNalType(keyframe, 7), "keyframe missing SPS")) {
    return false;
  }
  if (!Check(talk::ContainsNalType(keyframe, 8), "keyframe missing PPS")) {
    return false;
  }
  if (!Check(talk::ContainsNalType(keyframe, 5), "keyframe missing IDR")) {
    return false;
  }

  int width = 0;
  int height = 0;
  if (!Check(talk::ParseSpsSize(keyframe, &width, &height),
             "SPS parse failed")) {
    return false;
  }
  if (!Check(width == kWidth && height == kHeight, "SPS size mismatch")) {
    return false;
  }

  talk::MfH264Decoder decoder;
  std::vector<uint8_t> i420;
  bool decoded = false;
  for (const std::string& frame : frames) {
    int frame_width = 0;
    int frame_height = 0;
    if (!decoder.Decode(reinterpret_cast<const uint8_t*>(frame.data()),
                        frame.size(), 0, &i420, &frame_width, &frame_height)) {
      continue;
    }
    if (frame_width != kWidth || frame_height != kHeight) {
      std::printf("  decoded %dx%d (expected %dx%d)\n", frame_width,
                  frame_height, kWidth, kHeight);
    }
    if (!Check(frame_width == kWidth && frame_height == kHeight,
               "decoded size mismatch")) {
      return false;
    }
    if (!Check(i420.size() == static_cast<size_t>(kWidth) * kHeight * 3 / 2,
               "I420 length mismatch")) {
      return false;
    }
    decoded = true;
    break;
  }
  return Check(decoded, "decoder produced no frame");
}

}  // namespace

int main() {
  if (!CheckBitstreamTools()) {
    return 1;
  }
  if (!CheckCodecRoundTrip()) {
    return 1;
  }
  std::printf("video_codec_test: ok\n");
  return 0;
}
