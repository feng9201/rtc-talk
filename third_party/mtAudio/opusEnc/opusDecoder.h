#pragma once

#include <vector>
#include <string>
#include <opus/opus.h>


class OpusDecoderWrapper {
public:
    OpusDecoderWrapper(int sampleRate = 48000, int channels = 1);
    ~OpusDecoderWrapper();

    // 不允许拷贝
    OpusDecoderWrapper(const OpusDecoderWrapper&) = delete;
    OpusDecoderWrapper& operator=(const OpusDecoderWrapper&) = delete;

    // 允许移动
    OpusDecoderWrapper(OpusDecoderWrapper&&) noexcept;
    OpusDecoderWrapper& operator=(OpusDecoderWrapper&&) noexcept;

    // 是否创建成功
    bool isValid() const;

    // 最近一次错误描述
    std::string error() const;

    // 解码一个 Opus 数据包到 pcm 输出缓冲区（有符号 16 位）
    // 返回值：解码后的每声道样本数（>=0）或负的错误码
    // 如果传入 data==nullptr && len==0，会触发 PLC（packet loss concealment）返回插帧样本数
    int decode(const unsigned char* data, int len, std::vector<short>& out_pcm, int max_frame_samples = 5760, int decode_fec = 0);

    // 重置解码器内部状态
    void reset();

private:
    OpusDecoder* decoder_ = nullptr;
    int sample_rate_ = 48000;
    int channels_ = 2;
    std::string last_error_;
};
