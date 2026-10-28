#include "flv_demuxing.h"
#include "../multiplex_player_private.h"

#include <FlvParser.h>
#include <cpr/cpr.h>
#include <chrono>
#include <cstdio>
#include <w_log_cpp.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavcodec/packet.h>
}

namespace muxplay {

FlvDemuxer::FlvDemuxer(MultiplexPlayerPrivate* priv)
    : priv_(priv)
    , parser_(std::make_unique<FLVParser>())
    , cancelFlag_(std::make_shared<std::atomic_bool>(false))
{
}

FlvDemuxer::~FlvDemuxer()
{
    close();
}

bool FlvDemuxer::start(const char* url, int timeoutMs)
{
    if (!url || !url[0]) return false;

    // ── 在 startCloseMutex_ 保护下检查状态 + 重置 + 创建线程 ──
    // 这保证 close() 不会在我们检查/重置状态和创建线程之间插入
    std::unique_lock<std::mutex> lk(startCloseMutex_);
    if (closed_.load()) {
        LOG_ERR("[demuxing] [uuid={}] flv拉流停止,已触发关闭", priv_->uuid_);
        return false;
    }

    // 重置状态（closed_ 不重置 —— close() 设置的停止信号必须保留）
    isStop_.store(false);
    videoSeqReady_ = false;
    flvFrameCount_ = 0;
    flvByteCount_  = 0;
    flvFirstPts_   = 0;

    // 启动工作线程
    thread_ = std::make_unique<std::thread>(&FlvDemuxer::demuxLoop, this, std::string(url), timeoutMs);

    // 线程已创建，可以释放锁了（close() 此时如果进来，会 join 住等 demuxLoop 退出）
    lk.unlock();

    // 同步等待视频 seq header 到达
    {
        std::unique_lock<std::mutex> lock(seqMutex_);
        bool ok = seqCv_.wait_for(lock, std::chrono::milliseconds(timeoutMs), [this] {
            return videoSeqReady_ || isStop_.load() || closed_.load();
        });
        if (!ok || !videoSeqReady_) {
            LOG_ERR("[demuxing] [uuid={}] flv拉流超时等待视频或停止:{}", priv_->uuid_,isStop_.load());
            isStop_.store(true);
            return false;
        }
    }

    LOG_INFO("[demuxing] [uuid={}] flv拉流 header received", priv_->uuid_);
    return true;
}

void FlvDemuxer::close()
{
    LOG_INFO("[demuxing] [uuid={}] flv拉流开始停止", priv_->uuid_);
    // ── 分两步：1) 在锁内设置停止标志 + 取走 thread_  2) 在锁外 join ──
    // 这样 start() 和 close() 不会互相阻塞（join 可能要等 demuxLoop 退出，耗时较长）
    std::unique_ptr<std::thread> threadToJoin;
    {
        std::lock_guard<std::mutex> lk(startCloseMutex_);
        if (closed_.load()) return;  // 幂等
        closed_.store(true);
        isStop_.store(true);
        cancelFlag_->store(true);  // 主动取消 cpr HTTP 请求（连接阶段+传输阶段均可更快退出）
        // 取走 thread_ 所有权，锁外再 join
        threadToJoin = std::move(thread_);
    }

    // ── 锁已释放，start() 此时如果被调用，会看到 closed_=true 并立即返回 ──
    // 在锁外 join（不阻塞其他操作）
    if (threadToJoin) {
        LOG_INFO("[demuxing] [uuid={}] flv拉流线程检查join", priv_->uuid_);
        if (threadToJoin->joinable())
            threadToJoin->join();
        LOG_INFO("[demuxing] [uuid={}] flv拉流线程已join", priv_->uuid_);
    }

    // ── parser_->quit() 必须在 demuxLoop 线程 join 之后调用 ──
    // demuxLoop 线程在 WriteCallback 中持续调用 parser_->parse() 和 parser_->getPacket()，
    // 如果在 join 之前调用 quit()，会并发修改 parser_ 内部状态（_quit 标志 + _packets 队列），
    // 导致数据竞争崩溃。join 确保线程完全退出后再操作 parser_。
    if (parser_) {
        parser_->quit();
    }
    // 唤醒等待 seq header 的线程
    {
        std::lock_guard<std::mutex> slock(seqMutex_);
        seqCv_.notify_all();
    }
}

void FlvDemuxer::setAudioCallback(AudioPacketCallback cb)
{
    audioCallback_ = std::move(cb);
}

std::shared_ptr<FlvParser::Packet> FlvDemuxer::getVideoSeqHeader() const
{
    return parser_ ? parser_->getVideoSeqHeader() : nullptr;
}

std::shared_ptr<FlvParser::Packet> FlvDemuxer::getAudioSeqHeader() const
{
    return parser_ ? parser_->getAudioSeqHeader() : nullptr;
}

void FlvDemuxer::demuxLoop(const std::string& url, int connectTimeoutMs)
{
    fprintf(stderr, "[FlvDemuxer] demuxLoop started connectTimeout=%dms\n", connectTimeoutMs);

    // 使用 cpr::Session + 取消标志，支持 close() 主动中断 HTTP 请求
    // （progress callback 在连接/传输阶段均可检测到取消标志，比仅靠 WriteCallback return false 更及时）
    cpr::Session session;
    session.SetCancellationParam(cancelFlag_);
    session.SetUrl(cpr::Url{url});
    session.SetHeader(cpr::Header{{"Accept", "*/*"}});
    session.SetConnectTimeout(cpr::ConnectTimeout{connectTimeoutMs});  // TCP/SSL 握手超时
    session.SetLowSpeed(cpr::LowSpeed{1, 10});   // speed_limit=1 byte/s, time=10s
    session.SetVerifySsl(cpr::VerifySsl{false});  // 跳过 SSL 证书验证
    session.SetWriteCallback(cpr::WriteCallback{
            [this](std::string_view data, intptr_t /*userdata*/) -> bool {
                if (isStop_.load()) return false;

                // 喂入 flvparser 解析
                // 注意：parse() 可能因数据不足只解析了部分 header，此时 _header 仍为 nullptr
                // 后续 hasVideo()/hasAudio() 必须在 parse() 返回 true（header 解析成功）后才调用
                bool parsed = parser_->parse(reinterpret_cast<const uint8_t*>(data.data()), data.size());

                // 仅当 parse() 成功解析出 FLV header 后才查询流信息
                // FLVParser::hasVideo()/hasAudio() 内部访问 _header->hasVideo()/hasAudio()
                // 如果 _header 为 nullptr（header 尚未解析完成），直接调用会空指针崩溃
                if (!parsed) {
                    return !isStop_.load();
                }

                auto hasVideo = parser_->hasVideo();
                auto hasAudio = parser_->hasAudio();

                // 处理视频 seq header（仅在首次解析到时通知）
                if (hasVideo && !videoSeqReady_) {
                    auto seqHeader = parser_->getVideoSeqHeader();
                    if (seqHeader) {
                        fprintf(stderr, "[FlvDemuxer] video seq header received, codec_id=%d size=%u\n",
                                seqHeader->codec_id, seqHeader->size);
                        {
                            std::lock_guard<std::mutex> lock(seqMutex_);
                            videoSeqReady_ = true;
                        }
                        seqCv_.notify_one();
                    }
                }

                // 处理音频 seq header
                if (hasAudio) {
                    auto audioSeq = parser_->getAudioSeqHeader();
                    if (audioSeq) {
                    }
                }

                // 消费解析出的 packet 并转换成 AVPacket 入队
                static bool firstVidPktLogged = false;
                while (auto pkt = parser_->getPacket()) {
                    // 分离音频包
                    if (pkt->index == 8 /* FLV_TAG_TYPE_AUDIO */) {
                        if (audioCallback_) {
                            audioCallback_(pkt->pts, pkt->data, pkt->size, pkt->codec_id);
                        }
                        continue;
                    }

                    // 视频包 → 转 AVPacket 并入队
                    if (pkt->index == 9 /* FLV_TAG_TYPE_VIDEO */ && pkt->size > 0) {
                        if (!firstVidPktLogged) {
                            firstVidPktLogged = true;
                            fprintf(stderr, "[FlvDemuxer] first video pkt: size=%u codec_id=%d key=%d first 16 bytes: ",
                                    pkt->size, pkt->codec_id, (int)pkt->key_frame);
                            for (int i = 0; i < 16 && i < (int)pkt->size; ++i)
                                fprintf(stderr, "%02x ", pkt->data[i]);
                            fprintf(stderr, "\n");
                        }

                        // ── 码率统计（墙钟 1 秒，实际接收速率）──
                        if (flvByteCount_ == 0) {
                            flvStatsStartTime_ = std::chrono::steady_clock::now();
                        }
                        flvByteCount_ += pkt->size;
                        auto now = std::chrono::steady_clock::now();
                        auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                            now - flvStatsStartTime_).count();
                        if (elapsedMs >= 1000) {
                            double seconds = elapsedMs / 1000.0;
                            priv_->videoInfo_.bitrate = static_cast<int>(flvByteCount_ * 8 / seconds);
                            flvByteCount_  = 0;
                            flvStatsStartTime_ = now;
                        }

                        // ── 帧率统计（PTS 毫秒时间轴，标称帧率，一直计算）──
                        if (flvFrameCount_ == 0) {
                            flvFirstPts_ = pkt->pts;
                        }
                        flvFrameCount_++;
                        double durationMs = static_cast<double>(pkt->pts - flvFirstPts_);
                        if (durationMs >= 1000.0) {
                            double seconds = durationMs / 1000.0;
                            priv_->videoInfo_.frameRate = static_cast<float>((flvFrameCount_ - 1) / seconds);
                            flvFirstPts_   = pkt->pts;
                            flvFrameCount_ = 1;
                        }

                        AVPacket avpkt = {};
                        av_new_packet(&avpkt, static_cast<int>(pkt->size));
                        memcpy(avpkt.data, pkt->data, pkt->size);
                        avpkt.pts       = pkt->pts;
                        avpkt.dts       = pkt->pts;
                        avpkt.stream_index = 0;
                        if (pkt->key_frame)
                            avpkt.flags |= AV_PKT_FLAG_KEY;

                        priv_->enqueueVideoPacket(avpkt);
                        av_packet_unref(&avpkt);
                    }
                }

                return !isStop_.load();
            },
            0  // userdata (not used, we capture `this`)
        }
    );

    cpr::Response r = session.Get();
    if (r.error) {
        LOG_ERR("[demuxing] [uuid={}] flv拉流HTTP错误: code={}  message='{}'  status_code={}",
                priv_->uuid_, static_cast<int>(r.error.code), r.error.message, r.status_code);
    } else {
        LOG_INFO("[demuxing] [uuid={}] flv拉流HTTP结束 status_code={}", priv_->uuid_, r.status_code);
    }
    // 结束时通知，避免 start() 永久阻塞。
    // 若此时尚未拿到视频 seq header（HTTP 错误/连接中断等失败），置停止标志，
    // 让 start() 的 wait_for 立即返回失败，而非等待 5000ms 超时。
    {
        std::lock_guard<std::mutex> lock(seqMutex_);
        if (!videoSeqReady_) {
            isStop_.store(true);
        }
        seqCv_.notify_all();
    }
    LOG_INFO("[demuxing] [uuid={}] flv拉流线程退出", priv_->uuid_);
}

} // namespace muxplay
