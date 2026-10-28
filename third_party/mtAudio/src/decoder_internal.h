#pragma once

/**
 * @file decoder_internal.h
 * @brief mtAudio 内部解码器类声明（包内可见，不对外暴露）
 */

#include "../include/mt_audio_decoder.h"
#include <fdk-aac/aacdecoder_lib.h>
#include <opus/opus.h>
#include "audio_resampler.h"

namespace mtAudio {

// ── AAC 解码器 ──
class AacDecoder : public AudioDecoder {
public:
    AacDecoder();
    ~AacDecoder() override;
    bool init(const uint8_t* extraData, int extraDataLen,
              int sampleRate, int channels) override;
    int  decode(const uint8_t* data, int len,
                AudioFrame& outFrame, int64_t pts) override;
    void reset() override;

private:
    HANDLE_AACDECODER handle_   = nullptr;
    AudioResampler    resampler_;
    bool              inited_     = false;
    bool              infoReady_  = false;
};

// ── Opus 解码器 ──
class OpusDecoder : public AudioDecoder {
public:
    OpusDecoder();
    ~OpusDecoder() override;
    bool init(const uint8_t* extraData, int extraDataLen,
              int sampleRate, int channels) override;
    int  decode(const uint8_t* data, int len,
                AudioFrame& outFrame, int64_t pts) override;
    void reset() override;

private:
    ::OpusDecoder*  handle_  = nullptr;
    AudioResampler  resampler_;
    bool            inited_  = false;
};

// ── G.711 解码器 ──
class G711Decoder : public AudioDecoder {
public:
    explicit G711Decoder(AudioCodec codec);
    ~G711Decoder() override = default;
    bool init(const uint8_t* extraData, int extraDataLen,
              int sampleRate, int channels) override;
    int  decode(const uint8_t* data, int len,
                AudioFrame& outFrame, int64_t pts) override;
    void reset() override;

private:
    AudioCodec     codecType_;
    AudioResampler resampler_;
    bool           inited_ = false;
};

}  // namespace mtAudio
