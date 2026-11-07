#include "rtc_client/video/frame.h"

namespace rtc_client {

bool valid_frame_description(const FrameDescription& d) noexcept {
  if (d.fd < 0 || d.width == 0 || d.height == 0 || d.width > 8192 || d.height > 8192 ||
      d.width % 2 || d.height % 2 || d.format != PixelFormat::nv12 || d.modifier != 0 ||
      (d.rotation != 0 && d.rotation != 90 && d.rotation != 180 && d.rotation != 270) ||
      d.y_stride < d.width || d.uv_stride < d.width || d.allocated_y_rows < d.height ||
      d.allocated_y_rows % 2 || d.allocation_bytes == 0) return false;

  // Check multiplication with subtraction/division first, including malicious
  // large allocation metadata. Do not add offsets before proving they fit.
  if (d.y_offset > d.allocation_bytes ||
      d.allocated_y_rows > (d.allocation_bytes - d.y_offset) / d.y_stride) return false;
  const auto y_end = d.y_offset + std::uint64_t{d.y_stride} * d.allocated_y_rows;
  if (d.uv_offset < y_end || d.uv_offset > d.allocation_bytes ||
      d.allocated_y_rows / 2 > (d.allocation_bytes - d.uv_offset) / d.uv_stride) return false;
  return true;
}

std::shared_ptr<const DecodedFrame> DecodedFrame::create(FrameDescription d,
                                                       std::shared_ptr<void> owner) {
  if (!owner || !valid_frame_description(d)) return {};
  return std::shared_ptr<const DecodedFrame>(new DecodedFrame(d, std::move(owner)));
}

}  // namespace rtc_client
