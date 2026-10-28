#include "decoder_internal.h"

namespace mtAudio {

// ── 工厂方法 ──
std::unique_ptr<AudioDecoder> AudioDecoder::create(AudioCodec codec)
{
    switch (codec) {
    case AudioCodec::AAC:    return std::make_unique<AacDecoder>();
    case AudioCodec::Opus:   return std::make_unique<OpusDecoder>();
    case AudioCodec::G711A:
    case AudioCodec::G711U:  return std::make_unique<G711Decoder>(codec);
    }
    return nullptr;
}

}  // namespace mtAudio
