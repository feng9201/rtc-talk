#include "inject_source.h"
#include "multiplex_player_private.h"
#include "decoder/hw_decoder.h"
#include <tmcore/D3D11Utils.h>

#include <cstring>
#include <cstdio>
#include <chrono>
#include <thread>
#include <mutex>

extern "C" {
#include <libavcodec/avcodec.h>
}

namespace muxplay {

InjectSource::~InjectSource() = default;

bool InjectSource::prepare(const OpenContext& ctx)
{
	auto* p = player_->mux_private_;
	auto* epp_video_cb = ctx.extCallback;

	if (!epp_video_cb) return false;

	// 停止旧会话
	p->is_stop_ = true;
	p->packet_cv_.notify_all();
	p->frame_cv_.notify_all();
	if (p->decoderThread_ && p->decoderThread_->joinable()) {
		p->decoderThread_->join();
		p->decoderThread_.reset();
	}
	if (p->renderThread_ && p->renderThread_->joinable()) {
		p->renderThread_->join();
		p->renderThread_.reset();
	}
	player_->destryMux(true);
	p->is_stop_ = false;

	// 保存参数
	p->statusCallback_ = ctx.callback;
	p->user_data_ = ctx.userData;
	p->isInjectStream_.store(true, std::memory_order_release);
	p->extDecoderInited_.store(false, std::memory_order_release);
	p->extPtsCounter_ = 0;

	// 确定 AVCodecID（false=H264, true=H265）
	AVCodecID codecId = ctx.encodeType ? AV_CODEC_ID_HEVC : AV_CODEC_ID_H264;
	const AVCodec* codec = avcodec_find_decoder(codecId);
	if (!codec) {
		if (ctx.callback) ctx.callback(E_PLAYER_STATUS::PLAYER_STATUS_OPEN_FAILED, ctx.userData, "decoder not found");
		return false;
	}

	// 创建 AVCodecContext（extradata 由回调首次调用时设置）
	p->codecContext_ = avcodec_alloc_context3(codec);
	if (!p->codecContext_) {
		if (ctx.callback) ctx.callback(E_PLAYER_STATUS::PLAYER_STATUS_OPEN_FAILED, ctx.userData, "avcodec_alloc_context3 failed");
		return false;
	}

	// 创建 D3D11 设备
	if (!p->d3dDevice_) {
		ID3D11Device* device = nullptr;
		ID3D11DeviceContext* context = nullptr;
		if (tmcore::CreateD3D11Device(p->preferGpuIndex_, &device, &context)) {
			p->d3dDevice_ = device;
			p->d3dContext_ = context;
		}
	}

	// 重置帧间隔计时
	p->mediaClock_.lastVideoPtsMs = -1;
	p->mediaClock_.lastVideoWallTime = std::chrono::steady_clock::now();

	player_->player_status_ = E_PLAYER_STATUS::PLAYER_STATUS_OPENING;
	if (p->statusCallback_) {
		p->statusCallback_(player_->player_status_, p->user_data_, "inject opening");
	}

	// 注册 eventpp 回调
	p->extStreamCb_ = epp_video_cb;
	auto streamStartTime = std::chrono::steady_clock::now();
	const DecodeMode decodeMode = ctx.decodeMode;

	p->extStreamCbHandle_ = epp_video_cb->append(
		[this, codecId, codec, streamStartTime, decodeMode](
			uint8_t* head, int head_size,
			uint8_t* pkt, int pkt_size, bool is_key) {
			auto* p = player_->mux_private_;
			// close() 可能已触发
			if (p->is_stop_) return;

			// ── 阶段1：懒初始化（仅首次关键帧时执行）──
			if (!p->extDecoderInited_.load(std::memory_order_acquire)) {
				if (!is_key) return;  // 非关键帧时跳过，等待首个关键帧
				std::lock_guard<std::mutex> lock(p->extInitMutex_);
				if (!p->extDecoderInited_.load(std::memory_order_acquire)) {
					// 设置 extradata（SPS/PPS）
					if (head && head_size > 0) {
						p->codecContext_->extradata_size = head_size;
						p->codecContext_->extradata = (uint8_t*)av_mallocz(
							(size_t)head_size + AV_INPUT_BUFFER_PADDING_SIZE);
						memcpy(p->codecContext_->extradata, head, head_size);
					}

					// 初始化 HW 解码器
					auto hwDec = std::make_unique<HardwareDecoder>(p);
					if (!hwDec->init()) {
						fprintf(stderr, "[injectStream] HW decoder init failed\n");
						player_->player_status_ = E_PLAYER_STATUS::PLAYER_STATUS_DECODER_ERR;
						if (p->statusCallback_) {
							p->statusCallback_(player_->player_status_, p->user_data_, "hw decoder init failed");
						}
						return;
					}
					player_->decoder_ = std::move(hwDec);

					// ── 分辨率探测：Annex-B extradata 不会在 avcodec_open2 时解析出 w/h ──
					if (p->codecContext_->width <= 0 || p->codecContext_->height <= 0) {
						fprintf(stderr, "[injectStream] w/h=0 after init, probing from keyframe...\n");
						AVPacket probePkt;
						av_init_packet(&probePkt);
						if (av_new_packet(&probePkt, pkt_size) >= 0) {
							memcpy(probePkt.data, pkt, pkt_size);
							probePkt.flags = AV_PKT_FLAG_KEY;

							if (decodeMode == DecodeMode::Hardware) {
								std::lock_guard<std::mutex> d3dLock(p->d3dMutex_);
								avcodec_send_packet(p->codecContext_, &probePkt);
								AVFrame* probeFrame = av_frame_alloc();
								for (int i = 0; i < 20; ++i) {
									if (avcodec_receive_frame(p->codecContext_, probeFrame) >= 0) break;
									std::this_thread::sleep_for(std::chrono::milliseconds(5));
								}
								if (probeFrame->width > 0 && probeFrame->height > 0) {
									p->codecContext_->width = probeFrame->width;
									p->codecContext_->height = probeFrame->height;
									// 将探测帧推入帧队列，作为首帧渲染
									p->decoded_frame_queue_.try_enqueue(probeFrame);
									probeFrame = nullptr;
									fprintf(stderr, "[injectStream] probed resolution: w=%d h=%d\n",
										p->codecContext_->width, p->codecContext_->height);
								}
								if (probeFrame) av_frame_free(&probeFrame);
							}
							av_packet_unref(&probePkt);
						}
					}

					// 采集视频流参数
					p->video_width_  = p->codecContext_->width;
					p->video_height_ = p->codecContext_->height;
					p->videoInfo_.width     = p->video_width_;
					p->videoInfo_.height    = p->video_height_;
					p->videoInfo_.bitrate   = p->codecContext_->bit_rate;
					p->videoInfo_.frameRate = 0.0f;
					p->videoInfo_.codecName = avcodec_get_name(codecId);

					fprintf(stderr, "[injectStream] decoder init OK: w=%d h=%d\n",
						p->video_width_, p->video_height_);

					// ── 装配渲染管线（公共尾部：注入流无窗口、Live 模式）──
					if (!player_->setupRenderPipeline(nullptr, /*ptsIsMillis=*/true, /*liveMode=*/true, /*withOverlay=*/false)) {
						fprintf(stderr, "[injectStream] renderer init failed\n");
						player_->player_status_ = E_PLAYER_STATUS::PLAYER_STATUS_OPEN_FAILED;
						if (p->statusCallback_) {
							p->statusCallback_(player_->player_status_, p->user_data_, "renderer init failed");
						}
						return;
					}

					p->extDecoderInited_.store(true, std::memory_order_release);
					player_->player_status_ = E_PLAYER_STATUS::PLAYER_STATUS_OPENED;
					player_->streamingStarted_ = true;
					if (p->statusCallback_) {
						p->statusCallback_(player_->player_status_, p->user_data_, "inject opened");
					}
				}
			}

			// ── 阶段2：将编码数据包装为 AVPacket 入队 ──
			if (!pkt || pkt_size <= 0) return;
			if (!p->extDecoderInited_.load(std::memory_order_acquire)) return;

			AVPacket avpkt;
			av_init_packet(&avpkt);
			if (av_new_packet(&avpkt, pkt_size) < 0) return;
			memcpy(avpkt.data, pkt, pkt_size);
			avpkt.flags = is_key ? AV_PKT_FLAG_KEY : 0;

			// PTS：从流启动开始的壁钟毫秒时间
			auto now = std::chrono::steady_clock::now();
			avpkt.pts = avpkt.dts = std::chrono::duration_cast<std::chrono::milliseconds>(
				now - streamStartTime).count();

			p->enqueueVideoPacket(avpkt);
			av_packet_unref(&avpkt);
		});

	return true;
}

} // namespace muxplay
