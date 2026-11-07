#pragma once

#include <cstdint>
#include <memory>

namespace rtc_client {

enum class PixelFormat { nv12, unsupported };

// Borrowed DMA-BUF description. fd is valid only while owner is alive. Linear
// NV12 only; no assumption that the visible width equals the allocation stride.
struct FrameDescription {
  int fd{-1};
  std::uint32_t width{}, height{};
  std::uint32_t y_stride{}, uv_stride{}, allocated_y_rows{};
  std::uint64_t y_offset{}, uv_offset{}, allocation_bytes{};
  std::uint64_t modifier{};  // DRM_FORMAT_MOD_LINEAR = 0
  PixelFormat format{PixelFormat::nv12};
  unsigned rotation{};
  std::uint32_t rtp_timestamp{};
  std::int64_t render_time_us{};
  std::uint64_t generation{};
};

bool valid_frame_description(const FrameDescription&) noexcept;

class DecodedFrame final {
 public:
  // Returns null on invalid layout or empty ownership. Allocation may throw;
  // adapters must translate failures at their own C/SDK callback boundaries.
  static std::shared_ptr<const DecodedFrame> create(FrameDescription,
                                                   std::shared_ptr<void> owner);
  const FrameDescription& description() const noexcept { return description_; }

 private:
  DecodedFrame(FrameDescription d, std::shared_ptr<void> owner)
      : description_(d), owner_(std::move(owner)) {}
  const FrameDescription description_;
  // Decoder context/pool generation is retained through this opaque lease.
  const std::shared_ptr<void> owner_;
};

}  // namespace rtc_client
