#include "media.h"
namespace talk {
std::unique_ptr<Media> make_media(const Json&, const Json&, MuteGate, Media::Event) {
  throw std::runtime_error("该构建未包含 libwebrtc");
}
}
