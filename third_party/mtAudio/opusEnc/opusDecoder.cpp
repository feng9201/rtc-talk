#include "OpusDecoder.h"


OpusDecoderWrapper::OpusDecoderWrapper(int sampleRate, int channels)
    : sample_rate_(sampleRate), channels_(channels) {
    int err = OPUS_OK;
    decoder_ = opus_decoder_create(sample_rate_, channels_, &err);
    if (err != OPUS_OK) {
        last_error_ = opus_strerror(err);
        if (decoder_) {
            opus_decoder_destroy(decoder_);
            decoder_ = nullptr;
        }
    }
}

OpusDecoderWrapper::~OpusDecoderWrapper() {
    if (decoder_) {
        opus_decoder_destroy(decoder_);
        decoder_ = nullptr;
    }
}

OpusDecoderWrapper::OpusDecoderWrapper(OpusDecoderWrapper&& other) noexcept {
    decoder_ = other.decoder_;
    sample_rate_ = other.sample_rate_;
    channels_ = other.channels_;
    last_error_ = std::move(other.last_error_);
    other.decoder_ = nullptr;
}

OpusDecoderWrapper& OpusDecoderWrapper::operator=(OpusDecoderWrapper&& other) noexcept {
    if (this != &other) {
        if (decoder_) opus_decoder_destroy(decoder_);
        decoder_ = other.decoder_;
        sample_rate_ = other.sample_rate_;
        channels_ = other.channels_;
        last_error_ = std::move(other.last_error_);
        other.decoder_ = nullptr;
    }
    return *this;
}

bool OpusDecoderWrapper::isValid() const {
    return decoder_ != nullptr;
}

std::string OpusDecoderWrapper::error() const {
    return last_error_;
}

int OpusDecoderWrapper::decode(const unsigned char* data, int len, std::vector<short>& out_pcm, int max_frame_samples, int decode_fec) {
    if (!decoder_) return OPUS_INTERNAL_ERROR;

    out_pcm.resize(max_frame_samples * channels_);
    int ret = opus_decode(decoder_, data, len, out_pcm.data(), max_frame_samples, decode_fec);
    if (ret < 0) {
        last_error_ = opus_strerror(ret);
        return ret;
    }
    out_pcm.resize(ret * channels_);
    return ret;
}

void OpusDecoderWrapper::reset() {
    if (decoder_) {
        // opus 没有直接重置函数，销毁重建
        opus_decoder_destroy(decoder_);
        int err = OPUS_OK;
        decoder_ = opus_decoder_create(sample_rate_, channels_, &err);
        if (err != OPUS_OK) last_error_ = opus_strerror(err);
    }
}
