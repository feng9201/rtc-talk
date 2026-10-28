#include "ffmpeg_demuxing.h"
#include <libavutil/hwcontext_d3d11va.h>
#include <w_log_cpp.h>


namespace muxplay
{
	FFmpegDemuxing::FFmpegDemuxing(MultiplexPlayerPrivate* mux_private)
		:mux_private_(mux_private)
	{
	}

	FFmpegDemuxing::~FFmpegDemuxing()
	{
        if (hw_device_ctx_) {
            av_buffer_unref(&hw_device_ctx_);
        }
	}

	bool FFmpegDemuxing::startDemuxing()
	{
		AVCodecParameters* codecParameters = mux_private_->formatContext_->streams[mux_private_->videoStreamIndex_]->codecpar;
		const AVCodec* codec = avcodec_find_decoder(codecParameters->codec_id);
		if (!codec) {
            LOG_ERR("[demuxing] [uuid={}] 未找到解码器 codec_id={}", mux_private_->uuid_, (int)codecParameters->codec_id);
			return false;
		}
		// 创建解码器上下文
		mux_private_->codecContext_ = avcodec_alloc_context3(codec);
		if (avcodec_parameters_to_context(mux_private_->codecContext_, codecParameters) != 0) {
            LOG_ERR("[demuxing] [uuid={}] 转换编解码器参数失败", mux_private_->uuid_);
			return false;
		}

        // 在 codecContext 已经填充后读取宽高（更稳妥）
        int width = mux_private_->codecContext_->width;
        int height = mux_private_->codecContext_->height;
        // 备用：若 codecContext 未填充，可以退回到 codecParameters
        if (width <= 0 || height <= 0) {
            width = codecParameters->width;
            height = codecParameters->height;
        }
        // 保存到私有结构，其他模块可取用
        mux_private_->video_width_ = width;
        mux_private_->video_height_ = height;

        // [unknown] 这里强制创建了硬件解码器，但还有软件解码，待完善
        if (InitHWDecoder(codec, mux_private_->d3dDevice_)) {
            mux_private_->codecContext_->hw_device_ctx = av_buffer_ref(hw_device_ctx_);  // 创建一个对AVBuffer的新引用。
            mux_private_->codecContext_->get_format = &FFmpegDemuxing::GetHWFormat; // 由一些解码器调用，以选择将用于输出帧的像素格式
            mux_private_->codecContext_->opaque = this;
        }
        else {
            LOG_WARN("[demuxing] [uuid={}] 尝试使用硬件解码器但初始化失败", mux_private_->uuid_);
            return false;
        }
        //打开解码器
        if (avcodec_open2(mux_private_->codecContext_, codec, nullptr) < 0) {
            LOG_ERR("[demuxing] [uuid={}] 打开解码器失败", mux_private_->uuid_);
            return false;
        }

		demuxingThread_ = std::make_unique<std::thread>(&FFmpegDemuxing::demuxingLoop, this);
		return true;
	}

	bool FFmpegDemuxing::startDemuxingThread()
	{
		demuxingThread_ = std::make_unique<std::thread>(&FFmpegDemuxing::demuxingLoop, this);
		return true;
	}

    void FFmpegDemuxing::demuxingLoop()
    {
        LOG_DEBUG("[demuxing] [uuid={}] demux线程开始", mux_private_->uuid_);
        bitrateAccumBytes_ = 0;
        frameCount_ = 0;
        frameRateFirstPts_ = 0;
        bitrateLastCalcTime_ = std::chrono::steady_clock::now();
        // SPS/元数据已给出准确帧率时不再统计；否则持续用 PTS 统计并更新
        bool frameRateKnown = (mux_private_->videoInfo_.frameRate > 0.0f);
        while (mux_private_ && !mux_private_->is_stop_) {
            AVPacket packet;
            int ret = av_read_frame(mux_private_->formatContext_, &packet);
            if(ret<0){
                av_packet_unref(&packet);
				if (ret == AVERROR_EOF) {
                    LOG_INFO("[demuxing] [uuid={}] av_read_frame返回流结束", mux_private_->uuid_);
					break;  // 流结束，退出循环
				}
				std::this_thread::sleep_for(std::chrono::milliseconds(10)); 
                continue;
			}
            if (packet.stream_index == mux_private_->videoStreamIndex_) {
                // ── 运行时码率计算（墙钟 1 秒更新一次，码率是实际速率）──
                bitrateAccumBytes_ += packet.size;
                auto now = std::chrono::steady_clock::now();
                auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                    now - bitrateLastCalcTime_).count();
                if (elapsedMs >= 1000) {
                    int64_t bps = bitrateAccumBytes_ * 8 * 1000 / elapsedMs;
                    mux_private_->videoInfo_.bitrate = static_cast<int>(bps);
                    bitrateAccumBytes_ = 0;
                    bitrateLastCalcTime_ = now;
                }

                // ── 帧率计算：仅在 SPS 未给出准确帧率时进行 ──
                // 完全由 packet PTS 时间轴驱动（与网络速率无关）；SPS 无值时持续统计更新
                if (!frameRateKnown &&
                    packet.pts != AV_NOPTS_VALUE &&
                    mux_private_->formatContext_ && mux_private_->videoStreamIndex_ >= 0) {
                    if (frameCount_ == 0) {
                        frameRateFirstPts_ = packet.pts;
                    }
                    frameCount_++;

                    AVRational tb = mux_private_->formatContext_->streams[mux_private_->videoStreamIndex_]->time_base;
                    if (tb.den > 0) {
                        double durationSec = static_cast<double>(packet.pts - frameRateFirstPts_) * tb.num / tb.den;
                        if (durationSec >= 1.0) {
                            mux_private_->videoInfo_.frameRate =
                                static_cast<float>((frameCount_ - 1) / durationSec);
                            // 新窗口从当前帧开始（不丢帧）
                            frameRateFirstPts_ = packet.pts;
                            frameCount_ = 1;
                        }
                    }
                }
                // ── 限速：防止服务端缓存 GOP 瞬间涌入导致解码器过载 ──
                int tryCount = 5;
                while (mux_private_->getVideoPacketQueueSize() > 25 && tryCount-- >= 0) {
                    if (mux_private_->is_stop_) break;
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                    if (mux_private_->is_stop_) break;
                }
                mux_private_->enqueueVideoPacket(packet);
            }
            else if (packet.stream_index == mux_private_->audioStreamIndex_) {
                if (mux_private_->audioEnabled_) {
                    mux_private_->enqueueAudioPacket(packet);
                }
            }
            av_packet_unref(&packet);
        }
        LOG_INFO("[demuxing] [uuid={}] demux线程结束", mux_private_->uuid_);
    }

    AVPixelFormat FFmpegDemuxing::GetHWFormat(AVCodecContext* s, const enum AVPixelFormat* fmt)
    {
        auto decoder = static_cast<FFmpegDemuxing*>(s->opaque);
        const enum AVPixelFormat* p;
        for (p = fmt; *p != -1; p++){
            if (*p == decoder->pix_format_){
                return *p;
            }
        }
        return AV_PIX_FMT_NONE;
    }

    bool FFmpegDemuxing::InitHWDecoder(const AVCodec* codec, ID3D11Device* sharedDevice) 
    {
        if (!codec) return false;
        for (int i = 0; ; i++){
            const AVCodecHWConfig* config = avcodec_get_hw_config(codec, i);
            if (!config) return false;
            // 只使用 D3D11VA，跳过 DXVA2（否则 hwctx 类型不匹配，读 device_context 会访问冲突）
            if (config->device_type != AV_HWDEVICE_TYPE_D3D11VA)
                continue;
            if (config->methods & AV_CODEC_HW_CONFIG_METHOD_HW_DEVICE_CTX) {
                pix_format_ = config->pix_fmt;

                if (sharedDevice) {
                    // 使用外部传入的 D3D11 设备
                    hw_device_ctx_ = av_hwdevice_ctx_alloc(config->device_type);
                    if (!hw_device_ctx_) return false;
                    AVHWDeviceContext* hw_ctx = (AVHWDeviceContext*)hw_device_ctx_->data;
                    AVD3D11VADeviceContext* d3d11_ctx = (AVD3D11VADeviceContext*)hw_ctx->hwctx;
                    d3d11_ctx->device = sharedDevice;
                    sharedDevice->AddRef();
                    d3d11_ctx->device_context = mux_private_->d3dContext_;
                    if (d3d11_ctx->device_context)
                        d3d11_ctx->device_context->AddRef();
                    int ret = av_hwdevice_ctx_init(hw_device_ctx_);
                    if (ret < 0) {
                        av_buffer_unref(&hw_device_ctx_);
                        return false;
                    }
                } else {
                    // 让 FFmpeg 自行创建 D3D11 设备（内部处理 D3D11_CREATE_DEVICE_VIDEO_SUPPORT 等兼容性标志）
                    int ret = av_hwdevice_ctx_create(&hw_device_ctx_, config->device_type, nullptr, nullptr, 0);
                    if (ret < 0) return false;
                }

                // 从硬件设备上下文中提取 D3D11 设备，存入 mux_private_ 供渲染器零拷贝复用
                if (!mux_private_->d3dDevice_) {
                    AVHWDeviceContext* hw_ctx = (AVHWDeviceContext*)hw_device_ctx_->data;
                    AVD3D11VADeviceContext* d3d11_ctx = (AVD3D11VADeviceContext*)hw_ctx->hwctx;
                    if (d3d11_ctx->device) {
                        mux_private_->d3dDevice_ = d3d11_ctx->device;
                        mux_private_->d3dDevice_->AddRef();
                    }
                    if (d3d11_ctx->device_context) {
                        mux_private_->d3dContext_ = d3d11_ctx->device_context;
                        mux_private_->d3dContext_->AddRef();
                    }
                }
                return true;
            }
        }
    }

	void FFmpegDemuxing::close()
	{
        LOG_INFO("[demuxing] [uuid={}] 关闭 demuxing", mux_private_->uuid_);
		if(demuxingThread_){
            if(demuxingThread_->joinable()){
                demuxingThread_->join();
            }
            demuxingThread_.reset();
        }
        if (hw_device_ctx_) {
            av_buffer_unref(&hw_device_ctx_);
            hw_device_ctx_ = nullptr;  // 防止重复 close() 二次 unref
        }
	}
};
