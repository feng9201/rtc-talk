#include "opusEncoder.h"


OpusEncoderWrapper::OpusEncoderWrapper(int sampleRate, int channels, int application)
    : sample_rate_(sampleRate), channels_(channels), application_(application) {
    int err = OPUS_OK;
    encoder_ = opus_encoder_create(sample_rate_, channels_, application_, &err);
    if (err != OPUS_OK) {
        last_error_ = opus_strerror(err);
        if (encoder_) {
            opus_encoder_destroy(encoder_);
            encoder_ = nullptr;
        }
    }
}

OpusEncoderWrapper::~OpusEncoderWrapper() {
    if (encoder_) {
        opus_encoder_destroy(encoder_);
        encoder_ = nullptr;
    }
}

OpusEncoderWrapper::OpusEncoderWrapper(OpusEncoderWrapper&& other) noexcept {
    encoder_ = other.encoder_;
    sample_rate_ = other.sample_rate_;
    channels_ = other.channels_;
    application_ = other.application_;
    last_error_ = std::move(other.last_error_);
    other.encoder_ = nullptr;
}

OpusEncoderWrapper& OpusEncoderWrapper::operator=(OpusEncoderWrapper&& other) noexcept {
    if (this != &other) {
        if (encoder_) opus_encoder_destroy(encoder_);
        encoder_ = other.encoder_;
        sample_rate_ = other.sample_rate_;
        channels_ = other.channels_;
        application_ = other.application_;
        last_error_ = std::move(other.last_error_);
        other.encoder_ = nullptr;
    }
    return *this;
}

bool OpusEncoderWrapper::isValid() const {
    return encoder_ != nullptr;
}

std::string OpusEncoderWrapper::error() const {
    return last_error_;
}

int OpusEncoderWrapper::encodeFrame(const short* pcm, int frame_samples, std::vector<unsigned char>& out_packet, int max_packet_bytes) {
    if (!encoder_) return OPUS_INTERNAL_ERROR;
    if (!pcm || frame_samples <= 0) return OPUS_BAD_ARG;

    out_packet.resize(max_packet_bytes);
    // opus_encode 参数：encoder, pcm, frame_size (每通道采样数), out, max_data_bytes
    int ret = opus_encode(encoder_, pcm, frame_samples, out_packet.data(), max_packet_bytes);
    if (ret < 0) {
        last_error_ = opus_strerror(ret);
        out_packet.clear();
        return ret;
    }
    out_packet.resize(ret);
    return ret;
}

bool OpusEncoderWrapper::setBitrate(int bitrate_bps) {
    if (!encoder_) return false;

    opus_encoder_ctl(encoder_, OPUS_SET_VBR(0)); //0:CBR, 1:VBR(可变比特率 )
    opus_encoder_ctl(encoder_, OPUS_SET_COMPLEXITY(8));
    opus_encoder_ctl(encoder_, OPUS_SET_SIGNAL(OPUS_SIGNAL_MUSIC));
    int ret = opus_encoder_ctl(encoder_, OPUS_SET_BITRATE(bitrate_bps));
    if (ret != OPUS_OK) {
        last_error_ = opus_strerror(ret);
        return false;
    }
    return true;
}

void OpusEncoderWrapper::reset() {
    if (encoder_) {
        opus_encoder_destroy(encoder_);
        int err = OPUS_OK;
        encoder_ = opus_encoder_create(sample_rate_, channels_, application_, &err);
        if (err != OPUS_OK) last_error_ = opus_strerror(err);
    }
}


static int opus_write_callback(void* user_data, const unsigned char* ptr, opus_int32 len) {
    if (user_data) {
        OpusEncOgg* enc = (OpusEncOgg*)user_data;
        if (enc) {
            enc->enc_cb_(ptr,len,false);
        }
    }
    return 0;
}

static int opus_close_callback(void* user_data) {
    if (user_data) {
        OpusEncOgg* enc = (OpusEncOgg*)user_data;
        if (enc) {
            enc->enc_cb_(nullptr, 0, true);
        }
    }
	return 0;
}

OpusEncOgg::OpusEncOgg()
{
}

OpusEncOgg::~OpusEncOgg()
{
}

bool OpusEncOgg::initEnc(int sampleRate, int channels, int bitDep)
{
    channel_ = channels;
    sampleRate_ = sampleRate;
    comments_ = ope_comments_create();
    ope_comments_add(comments_, "ARTIST", "Someone");
    ope_comments_add(comments_, "TITLE", "Some track");
    ope_comments_add(comments_, "ENCODER", "libopusenc");
    ope_comments_add(comments_, "SAMPLERATE", std::to_string(sampleRate).c_str());
    ope_comments_add(comments_, "CHANNELS", std::to_string(channels).c_str());
    return true;
}

bool OpusEncOgg::encOpusLiveStream(encAudioCB enc_cb)
{
    int error = 0;
    OpusEncCallbacks cb;
    cb.write = opus_write_callback;
    cb.close = opus_close_callback;
    ogg_enc_ = ope_encoder_create_callbacks(&cb,this, comments_, sampleRate_, channel_,1, &error);
    if (!ogg_enc_) {
        return false;
    }
    //设置决策延迟（越小延迟越低 4800 = 100ms @ 48kHz
    ope_encoder_ctl(ogg_enc_, OPE_SET_DECISION_DELAY(4800));
    // 2. 设置复用延迟（影响Ogg页生成频率）
    ope_encoder_ctl(ogg_enc_, OPE_SET_MUXING_DELAY(2400));
    // 设置编码器参数（可选）
    ope_encoder_ctl(ogg_enc_, OPUS_SET_BITRATE(64000));      // 64kbps
    ope_encoder_ctl(ogg_enc_, OPUS_SET_COMPLEXITY(10));      // 最高质量
    ope_encoder_ctl(ogg_enc_, OPUS_SET_VBR(1));              // 启用VBR
    ope_encoder_ctl(ogg_enc_, OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE)); // 音乐信号
    //ope_encoder_ctl(enc, OPE_SET_EXPECTED_PACKET_DURATION(20));

    enc_cb_ = enc_cb;
    return true;
}

bool OpusEncOgg::sendLiveFrame(const char* sbuf, int len)
{
    int ret = ope_encoder_write(ogg_enc_, reinterpret_cast<const short*>(sbuf), len/2);
    if (ret != OPE_OK) {
        return false;
    }
    ope_encoder_flush_header(ogg_enc_);
    return true;
}

bool OpusEncOgg::encOpusByAduioFile(const std::string& filePath,const std::string& dstPath, int frame_size)
{
    FILE* fin;
    fin = fopen(filePath.c_str(), "rb");
    if (!fin) {
        last_err_ = "无法打开输入文件";
        return false;
    }
    // 获取文件大小，计算总采样数
    fseek(fin, 0, SEEK_END);
    long file_size = ftell(fin);
    rewind(fin);
    long total_samples = file_size / sizeof(short) / channel_;

    int error=0;
    ogg_enc_ = ope_encoder_create_file(
        dstPath.c_str(),
        comments_,
        sampleRate_,
        channel_,
        0,  // 0=应用类型: OPUS_APPLICATION_AUDIO
        &error
    );
    if (!ogg_enc_) {
        last_err_ = "创建编码器失败";
        fclose(fin);
        return false;
    }

    // 分配缓冲区
    short* buffer = (short*)malloc(frame_size * channel_ * sizeof(short));
    if (!buffer) {
        last_err_ = "分配内存失败";
        ope_encoder_destroy(ogg_enc_);
        ope_comments_destroy(comments_);
        fclose(fin);
        return false;
    }

    int frames_processed = 0;
    int total_written = 0;

    // 读取并编码
    while (!feof(fin)) {
        size_t samples_read = fread(buffer, sizeof(short), frame_size * channel_, fin);
        if (samples_read == 0) {
            break;  
        }
        // 如果最后一帧不满，用0填充
        if (samples_read < frame_size * channel_) {
            memset(buffer + samples_read, 0,
                (frame_size * channel_ - samples_read) * sizeof(short));
        }
        // 写入编码器
        int ret = ope_encoder_write(ogg_enc_, buffer, frame_size);
        if (ret != OPE_OK) {
            fprintf(stderr, "编码错误 (frame %d): %s\n",
                frames_processed, ope_strerror(ret));
            break;
        }
        ope_encoder_flush_header(ogg_enc_);
        frames_processed++;
        total_written += samples_read;
        // 进度显示
        if (frames_processed % 50 == 0) {
            float progress = (float)total_written / total_samples * 100;
            fflush(stdout);
        }
    }
    // 清理
    free(buffer);
    freeEnc();
    fclose(fin);
    return true;
}

void OpusEncOgg::freeEnc()
{
    if (ogg_enc_) {
        ope_encoder_drain(ogg_enc_);
        ope_encoder_destroy(ogg_enc_);
        ope_comments_destroy(comments_);
    }
}