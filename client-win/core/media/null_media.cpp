// File: core/media/null_media.cpp
// Brief: 未启用 libwebrtc 的构建替身。
#include "media/media.h"

#include <stdexcept>

namespace talk {

std::unique_ptr<Media> MakeMedia(const Json&, const Json&, MuteGate,
                                 Media::Event, VideoFrameSink) {
  throw std::runtime_error("该构建未包含 libwebrtc");
}

}  // namespace talk
