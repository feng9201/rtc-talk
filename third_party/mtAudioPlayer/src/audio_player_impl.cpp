#include "audio_player_internal.h"

#include <cstdio>
#include <cstring>
#include <thread>
#include <chrono>

namespace mtAudio {

// ═══════════════════════════════════════════════════════════
// 工厂
// ═══════════════════════════════════════════════════════════

std::unique_ptr<IAudioPlayer> IAudioPlayer::create(Backend backend)
{
    switch (backend) {
    case Backend::Miniaudio:
        return std::make_unique<MiniaudioPlayer>();
    }
    return nullptr;
}

// ═══════════════════════════════════════════════════════════
// MiniaudioPlayer 构造 / 析构
// ═══════════════════════════════════════════════════════════

MiniaudioPlayer::MiniaudioPlayer()
{
    memset(&device_, 0, sizeof(device_));
    memset(&deviceConfig_, 0, sizeof(deviceConfig_));
    memset(&ringBuffer_, 0, sizeof(ringBuffer_));
}

MiniaudioPlayer::~MiniaudioPlayer()
{
    closeDevice();
}

// ═══════════════════════════════════════════════════════════
// init
// ═══════════════════════════════════════════════════════════

bool MiniaudioPlayer::init(int sampleRate, int channels, int bitsPerSample)
{
    if (deviceInited_) {
        closeDevice();
    }

    sampleRate_    = sampleRate;
    channels_      = channels;
    bitsPerSample_ = bitsPerSample;

    // ── 格式转换 ──
    ma_format format;
    switch (bitsPerSample) {
    case 8:  format = ma_format_u8;  break;
    case 16: format = ma_format_s16; break;
    case 24: format = ma_format_s24; break;
    case 32: format = ma_format_s32; break;
    default:
        fprintf(stderr, "[MiniaudioPlayer] unsupported bitsPerSample=%d\n", bitsPerSample);
        return false;
    }

    // ── 环形缓冲区：缓存约 500ms 的 PCM 数据 ──
    ma_uint32 bufferFrames = (ma_uint32)(sampleRate * 0.5);
    ma_uint32 bufferSizeInBytes = bufferFrames * channels * ma_get_bytes_per_sample(format);
    ma_result result = ma_pcm_rb_init(format, (ma_uint32)channels, bufferFrames,
                                       nullptr, nullptr, &ringBuffer_);
    if (result != MA_SUCCESS) {
        fprintf(stderr, "[MiniaudioPlayer] ma_pcm_rb_init failed\n");
        return false;
    }

    // ── 设备配置 ──
    deviceConfig_ = ma_device_config_init(ma_device_type_playback);
    deviceConfig_.playback.format   = format;
    deviceConfig_.playback.channels = (ma_uint32)channels;
    deviceConfig_.sampleRate        = (ma_uint32)sampleRate;
    deviceConfig_.dataCallback      = dataCallback;
    deviceConfig_.pUserData         = this;

    // 使用默认设备
    result = ma_device_init(nullptr, &deviceConfig_, &device_);
    if (result != MA_SUCCESS) {
        fprintf(stderr, "[MiniaudioPlayer] ma_device_init failed: %d\n", result);
        ma_pcm_rb_uninit(&ringBuffer_);
        return false;
    }

    deviceInited_ = true;
    stopped_ = false;
    fprintf(stderr, "[MiniaudioPlayer] init ok: %dHz/%dch/%dbit\n",
            sampleRate, channels, bitsPerSample);
    return true;
}

// ═══════════════════════════════════════════════════════════
// pushFrame
// ═══════════════════════════════════════════════════════════

bool MiniaudioPlayer::pushFrame(const AudioFrame& frame)
{
    if (!deviceInited_ || frame.empty()) return false;
    if (stopped_.load()) return false;

    // 格式兼容性检查
    if (frame.sampleRate    != sampleRate_ ||
        frame.channels      != channels_   ||
        frame.bitsPerSample != bitsPerSample_) {
        fprintf(stderr, "[MiniaudioPlayer] format mismatch: frame(%d/%d/%d) vs device(%d/%d/%d)\n",
                frame.sampleRate, frame.channels, frame.bitsPerSample,
                sampleRate_, channels_, bitsPerSample_);
        return false;
    }

    // ── 写入环形缓冲区（阻塞等待空间，实现解码→播放的自然背压控制）──
    // 关键设计：音频解码线程由 miniaudio 设备回调自然调速。
    // 环形缓冲区满时阻塞等待（而非丢弃帧），避免音频数据丢失和 PTS 跟踪断裂。
    // 分片写入：支持环形缓冲区 wrap-around 时的不连续空间。
    ma_uint32 bytesPerFrame = ma_get_bytes_per_frame(ringBuffer_.format, ringBuffer_.channels);
    size_t bytesToWrite = (size_t)frame.numSamples * bytesPerFrame;

    size_t written = 0;
    while (!stopped_.load() && written < bytesToWrite) {
        size_t acquired = bytesToWrite - written;
        void* pWriteBuf = nullptr;
        ma_result result = ma_rb_acquire_write(&ringBuffer_.rb, &acquired, &pWriteBuf);
        if (result == MA_SUCCESS && pWriteBuf && acquired > 0) {
            memcpy(pWriteBuf, frame.data.data() + written, acquired);
            ma_rb_commit_write(&ringBuffer_.rb, acquired);
            written += acquired;
        } else {
            // 无可用空间 → 等待音频设备回调消费数据（5ms 轮询，stopped_ 可中断）
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    }
    if (stopped_.load()) return false;

    // ── 记录 PTS 标记 ──
    {
        std::lock_guard<std::mutex> lock(ptsMutex_);
        if (ptsQueue_.size() > 128) {
            ptsQueue_.pop_front();
        }
        ptsQueue_.push_back({frame.pts, frame.numSamples});
    }

    return true;
}

// ═══════════════════════════════════════════════════════════
// 播放控制
// ═══════════════════════════════════════════════════════════

void MiniaudioPlayer::play()
{
    if (!deviceInited_) return;

    if (ma_device_start(&device_) == MA_SUCCESS) {
        playing_.store(true);
        stopped_.store(false);
        fprintf(stderr, "[MiniaudioPlayer] play started\n");
    }
}

void MiniaudioPlayer::pause()
{
    if (!deviceInited_) return;
    playing_.store(false);
    // ma_device_stop 会停止回调；再次 start 可从断点继续
    // 但这里简单停止设备以避免缓冲区持续消耗
    (void)ma_device_stop(&device_);
    fprintf(stderr, "[MiniaudioPlayer] paused\n");
}

void MiniaudioPlayer::stop()
{
    if (!deviceInited_) return;
    playing_.store(false);
    stopped_.store(true);

    (void)ma_device_stop(&device_);

    // 清空缓冲区
    ma_pcm_rb_reset(&ringBuffer_);

    // 清空 PTS 队列
    {
        std::lock_guard<std::mutex> lock(ptsMutex_);
        ptsQueue_.clear();
    }
    currentPts_.store(-1);

    fprintf(stderr, "[MiniaudioPlayer] stopped\n");
}

void MiniaudioPlayer::setVolume(float vol)
{
    volume_ = (vol < 0.0f) ? 0.0f : (vol > 1.0f ? 1.0f : vol);
    if (deviceInited_) {
        ma_device_set_master_volume(&device_, volume_);
    }
}

bool MiniaudioPlayer::isPlaying() const
{
    return playing_.load();
}

int64_t MiniaudioPlayer::getPlayingPts() const
{
    return currentPts_.load();
}

// ═══════════════════════════════════════════════════════════
// 内部方法
// ═══════════════════════════════════════════════════════════

void MiniaudioPlayer::closeDevice()
{
    if (deviceInited_) {
        ma_device_stop(&device_);
        playing_.store(false);
        ma_device_uninit(&device_);
        deviceInited_ = false;
    }
    ma_pcm_rb_uninit(&ringBuffer_);
}

void MiniaudioPlayer::dataCallback(ma_device* pDevice, void* pOutput,
                                    const void* /*pInput*/, ma_uint32 frameCount)
{
    auto* self = static_cast<MiniaudioPlayer*>(pDevice->pUserData);
    if (self) {
        self->onDataCallback(pOutput, frameCount);
    }
}

void MiniaudioPlayer::onDataCallback(void* pOutput, ma_uint32 frameCount)
{
    if (stopped_.load()) {
        ma_uint32 bpf = ma_get_bytes_per_frame(ringBuffer_.format, ringBuffer_.channels);
        memset(pOutput, 0, frameCount * bpf);
        return;
    }

    // ── 从环形缓冲区读取 ──
    ma_uint32 bytesPerFrame = ma_get_bytes_per_frame(ringBuffer_.format, ringBuffer_.channels);
    size_t bytesToRead = (size_t)frameCount * bytesPerFrame;

    void* pReadBuf = nullptr;
    ma_result result = ma_rb_acquire_read(&ringBuffer_.rb, &bytesToRead, &pReadBuf);

    if (result != MA_SUCCESS || !pReadBuf || bytesToRead == 0) {
        // 缓冲区无数据 → 输出静音
        memset(pOutput, 0, (size_t)frameCount * bytesPerFrame);
        currentPts_.store(-1);
        return;
    }

    // 拷贝到输出
    memcpy(pOutput, pReadBuf, bytesToRead);
    ma_rb_commit_read(&ringBuffer_.rb, bytesToRead);

    // 若未读满，剩余部分填静音
    size_t expectedBytes = (size_t)frameCount * bytesPerFrame;
    if (bytesToRead < expectedBytes) {
        memset((uint8_t*)pOutput + bytesToRead, 0, expectedBytes - bytesToRead);
    }

    // ── 更新当前 PTS ──
    ma_uint32 framesRead = (ma_uint32)(bytesToRead / bytesPerFrame);
    int64_t newPts = consumePts((int)framesRead);
    if (newPts >= 0) {
        currentPts_.store(newPts);
    }
}

int64_t MiniaudioPlayer::consumePts(int frames)
{
    std::lock_guard<std::mutex> lock(ptsMutex_);
    int64_t lastValidPts = currentPts_.load();

    while (frames > 0 && !ptsQueue_.empty()) {
        auto& entry = ptsQueue_.front();
        if (entry.numFrames <= frames) {
            lastValidPts = entry.pts;
            frames -= entry.numFrames;
            ptsQueue_.pop_front();
        } else {
            // 部分消费
            lastValidPts = entry.pts;
            entry.numFrames -= frames;
            frames = 0;
        }
    }

    return lastValidPts;
}

}  // namespace mtAudio
