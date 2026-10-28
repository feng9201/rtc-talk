#pragma once

#include <vector>
#include <string>
#include <functional>
#include <opus/opus.h>
#include <opusenc.h>


class OpusEncoderWrapper {
public:
    OpusEncoderWrapper(int sampleRate = 48000, int channels = 1, int application = OPUS_APPLICATION_VOIP);//OPUS_APPLICATION_VOIP OPUS_APPLICATION_AUDIO
    ~OpusEncoderWrapper();

    // 不允许拷贝
    OpusEncoderWrapper(const OpusEncoderWrapper&) = delete;
    OpusEncoderWrapper& operator=(const OpusEncoderWrapper&) = delete;

    // 允许移动
    OpusEncoderWrapper(OpusEncoderWrapper&&) noexcept;
    OpusEncoderWrapper& operator=(OpusEncoderWrapper&&) noexcept;

    // 是否创建成功
    bool isValid() const;

    // 最近一次错误描述
    std::string error() const;

    // 编码一帧 PCM（16-bit 有符号），输入为每通道的采样数 frame_samples（比如 960 表示 20ms @48k）
    // out_packet 将被填充为编码输出字节流
    // 返回值：编码后包长度（>0）或负的错误码
    int encodeFrame(const short* pcm, int frame_samples, std::vector<unsigned char>& out_packet, int max_packet_bytes = 4000);

    // 设置比特率（bps）
    bool setBitrate(int bitrate_bps);

    // 重置编码器状态（销毁重建）
    void reset();
private:
    OpusEncoder* encoder_ = nullptr;
    int sample_rate_ = 48000;
    int channels_ = 1;
    int application_ = OPUS_APPLICATION_AUDIO;
    std::string last_error_;
};

// 生成可播放的opus
class OpusEncOgg {
public:
    OpusEncOgg();
    ~OpusEncOgg();

    using encAudioCB = std::function<void(const unsigned char* ptr, int len,bool close)>;

    bool initEnc(int sampleRate = 48000, int channels = 1, int bitDep=16);
    bool encOpusByAduioFile(const std::string& filePath,const std::string& dstPath,int frame_size = 960);
    bool encOpusLiveStream(encAudioCB enc_cb);
    bool sendLiveFrame(const char* sbuf,int len);

    void freeEnc();

    encAudioCB       enc_cb_ = nullptr;
private:
    OggOpusComments* comments_ =nullptr;
    OggOpusEnc*      ogg_enc_ = nullptr;
    int              channel_ = 1;
    int              sampleRate_ = 48000;
    
    std::string      last_err_;
};