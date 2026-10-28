#pragma once

#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <functional>
#include <chrono>

struct AVPacket;

namespace FlvParser {
    struct Packet;
}
class FLVParser;

namespace muxplay {

struct MultiplexPlayerPrivate;

// ── HTTP-FLV 解封装器 ──
// 负责 cpr HTTP 拉流 + flvparser 解析 + 输出 AVPacket 到 vid_packetQueue_
// 与 FFmpegDemuxing 平级，共用下游的解码/渲染管线
//
// 线程安全设计：
// - startCloseMutex_ 保护 start()/close() 的状态转换，确保 close() 不会在
//   start() 检查状态和创建线程之间插入
// - close() 分两步：锁内设置标志+取走thread_，锁外join（避免持锁join阻塞start()）
// - parser_->quit() 只在 demuxLoop join 之后调用，且只在 demuxLoop 确实运行过时调用
class FlvDemuxer {
public:
    explicit FlvDemuxer(MultiplexPlayerPrivate* priv);
    ~FlvDemuxer();

    /// 启动拉流线程（阻塞直到完成 HTTP 握手并获取到视频 seq header，或超时）
    /// 返回 false 表示启动失败（连接错误 / 超时）
    bool start(const char* url, int timeoutMs = 5000);

    /// 停止拉流，join 线程
    void close();

    /// 获取视频/音频序列头（仅在 start() 成功返回后有效）
    std::shared_ptr<FlvParser::Packet> getVideoSeqHeader() const;
    std::shared_ptr<FlvParser::Packet> getAudioSeqHeader() const;

    /// 音频包回调类型（预留接口，当前版本暂不调用）:
    /// 参数: pts, data, size, codec_id
    using AudioPacketCallback = std::function<void(int64_t pts, const uint8_t* data, uint32_t size, int32_t codec_id)>;
    void setAudioCallback(AudioPacketCallback cb);

private:
    void demuxLoop(const std::string& url, int connectTimeoutMs);

    MultiplexPlayerPrivate* priv_ = nullptr;
    std::unique_ptr<FLVParser> parser_;
    std::unique_ptr<std::thread> thread_;
    std::atomic<bool> isStop_{false};

    // cpr HTTP 请求取消标志（close() 设置 → progress callback 检测 → 中断传输）
    // 比 WriteCallback return false 更及时：连接/等待响应头阶段也能中断
    std::shared_ptr<std::atomic_bool> cancelFlag_;

    // 同步：等待视频 seq header 到达
    std::mutex              seqMutex_;
    std::condition_variable seqCv_;
    bool                    videoSeqReady_ = false;

    // start()/close() 互斥：保护状态检查+线程创建/取走的原子性
    // close() 在锁内取走 thread_，在锁外 join，避免持锁 join 阻塞 start()
    std::mutex        startCloseMutex_;
    std::atomic<bool> closed_{false};

    // 音频回调（预留）
    AudioPacketCallback audioCallback_;

    // FLV 码率统计（墙钟 1 秒）与帧率统计（PTS 毫秒时间轴）
    std::chrono::steady_clock::time_point flvStatsStartTime_;
    int     flvFrameCount_ = 0;
    int64_t flvByteCount_  = 0;
    int64_t flvFirstPts_   = 0;
};

} // namespace muxplay
