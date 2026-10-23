// File: core/media/webrtc/video/h264_bitstream.cpp
// Brief: 实现 AVCC/Annex-B 归一与 SPS 尺寸解析。
#include "media/webrtc/video/h264_bitstream.h"

#include <cstring>
#include <vector>

namespace talk {
namespace {

constexpr size_t kNoPos = static_cast<size_t>(-1);
constexpr char kStartCode[4] = {0, 0, 0, 1};

// 3 字节 00 00 01 与 4 字节 00 00 00 01 都算起始码。
size_t FindStartCode(const uint8_t* data, size_t size, size_t from,
                     size_t* prefix) {
  for (size_t i = from; i + 3 <= size; ++i) {
    if (data[i] != 0 || data[i + 1] != 0) {
      continue;
    }
    if (data[i + 2] == 1) {
      *prefix = 3;
      return i;
    }
    if (i + 4 <= size && data[i + 2] == 0 && data[i + 3] == 1) {
      *prefix = 4;
      return i;
    }
  }
  return kNoPos;
}

// 遍历 Annex-B 中的 NAL 负载（不含起始码与尾部的 0 填充）。
template <typename F>
void ForEachNal(const uint8_t* data, size_t size, F&& visit) {
  size_t offset = 0;
  while (offset < size) {
    size_t prefix = 0;
    const size_t start = FindStartCode(data, size, offset, &prefix);
    if (start == kNoPos) {
      return;
    }
    const size_t payload = start + prefix;
    size_t next_prefix = 0;
    const size_t next = FindStartCode(data, size, payload, &next_prefix);
    size_t end = next == kNoPos ? size : next;
    while (end > payload && data[end - 1] == 0) {
      --end;
    }
    if (end > payload) {
      visit(data + payload, end - payload);
    }
    if (next == kNoPos) {
      return;
    }
    offset = next;
  }
}

// 去掉防竞争字节（00 00 03 -> 00 00），供 SPS 按位解析使用。
std::vector<uint8_t> ToRbsp(const uint8_t* data, size_t size) {
  std::vector<uint8_t> out;
  out.reserve(size);
  int zeros = 0;
  for (size_t i = 0; i < size; ++i) {
    const uint8_t byte = data[i];
    if (zeros >= 2 && byte == 3) {
      zeros = 0;
      continue;
    }
    out.push_back(byte);
    zeros = byte == 0 ? zeros + 1 : 0;
  }
  return out;
}

class BitReader {
 public:
  BitReader(const uint8_t* data, size_t size) : data_(data), size_(size) {}

  bool ReadBits(int count, uint32_t* value) {
    uint32_t result = 0;
    for (int i = 0; i < count; ++i) {
      if (bit_ >= size_ * 8) {
        return false;
      }
      const uint32_t bit =
          (data_[bit_ / 8] >> (7 - (bit_ % 8))) & 0x1u;
      ++bit_;
      result = (result << 1) | bit;
    }
    *value = result;
    return true;
  }

  bool ReadUe(uint32_t* value) {
    int zeros = 0;
    for (;;) {
      uint32_t bit = 0;
      if (!ReadBits(1, &bit)) {
        return false;
      }
      if (bit != 0) {
        break;
      }
      if (++zeros > 31) {
        return false;
      }
    }
    uint32_t rest = 0;
    if (zeros > 0 && !ReadBits(zeros, &rest)) {
      return false;
    }
    *value = ((1u << zeros) - 1) + rest;
    return true;
  }

  bool ReadSe(int32_t* value) {
    uint32_t code = 0;
    if (!ReadUe(&code)) {
      return false;
    }
    *value = (code & 1u) ? static_cast<int32_t>((code + 1) / 2)
                         : -static_cast<int32_t>(code / 2);
    return true;
  }

 private:
  const uint8_t* data_;
  size_t size_;
  size_t bit_ = 0;
};

bool IsHighProfile(uint32_t profile_idc) {
  switch (profile_idc) {
    case 100:
    case 110:
    case 122:
    case 244:
    case 44:
    case 83:
    case 86:
    case 118:
    case 128:
    case 138:
    case 139:
    case 134:
    case 135:
      return true;
    default:
      return false;
  }
}

// 高等级 profile 的缩放矩阵；只需按语法跳过。
bool SkipScalingList(BitReader& reader, int size) {
  int32_t last = 8;
  int32_t next = 8;
  for (int i = 0; i < size; ++i) {
    if (next != 0) {
      if (!reader.ReadSe(&next)) {
        return false;
      }
      last = (last + next + 256) % 256;
    }
  }
  return true;
}

}  // namespace

std::string ToAnnexB(const uint8_t* data, size_t size) {
  if (data == nullptr || size == 0) {
    return std::string();
  }
  const bool already_annexb =
      (size >= 4 && data[0] == 0 && data[1] == 0 && data[2] == 0 &&
       data[3] == 1) ||
      (size >= 3 && data[0] == 0 && data[1] == 0 && data[2] == 1);
  if (already_annexb) {
    return std::string(reinterpret_cast<const char*>(data), size);
  }

  // AVCC：每个 NAL 前是 4 字节大端长度。
  std::string out;
  size_t offset = 0;
  while (offset + 4 <= size) {
    const uint32_t length = (static_cast<uint32_t>(data[offset]) << 24) |
                            (static_cast<uint32_t>(data[offset + 1]) << 16) |
                            (static_cast<uint32_t>(data[offset + 2]) << 8) |
                            static_cast<uint32_t>(data[offset + 3]);
    offset += 4;
    if (length == 0 || length > size - offset) {
      return std::string();
    }
    out.append(kStartCode, sizeof(kStartCode));
    out.append(reinterpret_cast<const char*>(data + offset), length);
    offset += length;
  }
  if (offset != size) {
    return std::string();
  }
  return out;
}

void CollectParameterSets(const std::string& annexb, std::string* sps,
                          std::string* pps) {
  if (sps == nullptr || pps == nullptr) {
    return;
  }
  const auto* data = reinterpret_cast<const uint8_t*>(annexb.data());
  ForEachNal(data, annexb.size(), [&](const uint8_t* nal, size_t size) {
    const uint8_t type = nal[0] & 0x1f;
    if (type != 7 && type != 8) {
      return;
    }
    std::string& target = type == 7 ? *sps : *pps;
    target.assign(kStartCode, sizeof(kStartCode));
    target.append(reinterpret_cast<const char*>(nal), size);
  });
}

bool ContainsNalType(const std::string& annexb, uint8_t nal_type) {
  bool found = false;
  const auto* data = reinterpret_cast<const uint8_t*>(annexb.data());
  ForEachNal(data, annexb.size(), [&](const uint8_t* nal, size_t) {
    if ((nal[0] & 0x1f) == nal_type) {
      found = true;
    }
  });
  return found;
}

bool ParseSpsSize(const std::string& annexb, int* width, int* height) {
  if (width == nullptr || height == nullptr) {
    return false;
  }
  const uint8_t* sps = nullptr;
  size_t sps_size = 0;
  const auto* data = reinterpret_cast<const uint8_t*>(annexb.data());
  ForEachNal(data, annexb.size(), [&](const uint8_t* nal, size_t size) {
    if (sps == nullptr && (nal[0] & 0x1f) == 7 && size > 4) {
      sps = nal;
      sps_size = size;
    }
  });
  if (sps == nullptr) {
    return false;
  }

  const std::vector<uint8_t> rbsp = ToRbsp(sps + 1, sps_size - 1);
  BitReader reader(rbsp.data(), rbsp.size());
  uint32_t profile_idc = 0;
  uint32_t ignored = 0;
  uint32_t chroma_format_idc = 1;
  bool separate_colour_plane = false;
  if (!reader.ReadBits(8, &profile_idc) || !reader.ReadBits(8, &ignored) ||
      !reader.ReadBits(8, &ignored) || !reader.ReadUe(&ignored)) {
    return false;
  }
  if (IsHighProfile(profile_idc)) {
    if (!reader.ReadUe(&chroma_format_idc)) {
      return false;
    }
    if (chroma_format_idc == 3) {
      if (!reader.ReadBits(1, &ignored)) {
        return false;
      }
      separate_colour_plane = ignored != 0;
    }
    uint32_t skip = 0;
    if (!reader.ReadUe(&skip) || !reader.ReadUe(&skip) ||
        !reader.ReadBits(1, &ignored)) {
      return false;
    }
    uint32_t scaling_matrix = 0;
    if (!reader.ReadBits(1, &scaling_matrix)) {
      return false;
    }
    if (scaling_matrix != 0) {
      const int lists = chroma_format_idc == 3 ? 12 : 8;
      for (int i = 0; i < lists; ++i) {
        uint32_t present = 0;
        if (!reader.ReadBits(1, &present)) {
          return false;
        }
        if (present != 0 && !SkipScalingList(reader, i < 6 ? 16 : 64)) {
          return false;
        }
      }
    }
  }

  uint32_t log2_max_frame_num = 0;
  uint32_t pic_order_cnt_type = 0;
  if (!reader.ReadUe(&log2_max_frame_num) ||
      !reader.ReadUe(&pic_order_cnt_type)) {
    return false;
  }
  if (pic_order_cnt_type == 0) {
    uint32_t log2_max_poc = 0;
    if (!reader.ReadUe(&log2_max_poc)) {
      return false;
    }
  } else if (pic_order_cnt_type == 1) {
    uint32_t flags = 0;
    int32_t se = 0;
    if (!reader.ReadBits(1, &flags) || !reader.ReadSe(&se) ||
        !reader.ReadSe(&se)) {
      return false;
    }
    uint32_t cycle = 0;
    if (!reader.ReadUe(&cycle)) {
      return false;
    }
    for (uint32_t i = 0; i < cycle; ++i) {
      if (!reader.ReadSe(&se)) {
        return false;
      }
    }
  }

  uint32_t max_num_ref_frames = 0;
  uint32_t gaps_allowed = 0;
  uint32_t width_mbs = 0;
  uint32_t height_units = 0;
  uint32_t frame_mbs_only = 0;
  if (!reader.ReadUe(&max_num_ref_frames) ||
      !reader.ReadBits(1, &gaps_allowed) || !reader.ReadUe(&width_mbs) ||
      !reader.ReadUe(&height_units) ||
      !reader.ReadBits(1, &frame_mbs_only)) {
    return false;
  }
  if (frame_mbs_only == 0 && !reader.ReadBits(1, &ignored)) {
    return false;
  }
  if (!reader.ReadBits(1, &ignored)) {
    return false;
  }

  uint32_t crop_left = 0;
  uint32_t crop_right = 0;
  uint32_t crop_top = 0;
  uint32_t crop_bottom = 0;
  uint32_t cropping = 0;
  if (!reader.ReadBits(1, &cropping)) {
    return false;
  }
  if (cropping != 0) {
    if (!reader.ReadUe(&crop_left) || !reader.ReadUe(&crop_right) ||
        !reader.ReadUe(&crop_top) || !reader.ReadUe(&crop_bottom)) {
      return false;
    }
  }

  const uint32_t chroma = separate_colour_plane ? 0 : chroma_format_idc;
  const uint32_t sub_width = (chroma == 1 || chroma == 2) ? 2 : 1;
  const uint32_t sub_height = chroma == 1 ? 2 : 1;
  const int64_t coded_width = (static_cast<int64_t>(width_mbs) + 1) * 16;
  const int64_t coded_height =
      (static_cast<int64_t>(height_units) + 1) * 16 * (2 - frame_mbs_only);
  const int64_t crop_x = static_cast<int64_t>(crop_left + crop_right) * sub_width;
  const int64_t crop_y =
      static_cast<int64_t>(crop_top + crop_bottom) * sub_height *
      (2 - frame_mbs_only);
  const int64_t out_width = coded_width - crop_x;
  const int64_t out_height = coded_height - crop_y;
  if (out_width <= 0 || out_height <= 0 || out_width > 8192 ||
      out_height > 8192) {
    return false;
  }
  *width = static_cast<int>(out_width);
  *height = static_cast<int>(out_height);
  return true;
}

}  // namespace talk
