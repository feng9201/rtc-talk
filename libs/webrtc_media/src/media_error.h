#pragma once

#include "rtcwin/webrtc_media/media_session.h"

#include <string>
#include <utility>

namespace rtcwin::webrtc_media {

inline void SetError(MediaError* error, int code, std::string message) {
  if (error == nullptr) return;
  error->code = code;
  error->message = std::move(message);
}

}  // namespace rtcwin::webrtc_media
