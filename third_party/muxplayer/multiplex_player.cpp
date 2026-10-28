#include "multiplex_player.h"
#include <thread>
#include <memory>
#include <cstdio>

#include <w_log_cpp.h>
#include <mttool/TimeTicker.h>
#include <mttool/onceToken.h>
#include "multiplex_player_private.h"
#include "demuxing/ffmpeg_demuxing.h"
#include "decoder/ffmpeg_decoder.h"
#include "decoder/hw_decoder.h"
#include "decoder/sw_decoder.h"
#include "render/dx_render.h"
#include "render/nv12_share.h"
#include "render/d3d11_renderer.h"
#include "render/frame_pump.h"
#include "render/overlay_manager.h"
#include <tmcore/D3D11Utils.h>
#include <windows.h>      // GetDpiForWindow
#include <mt_audio_decoder.h>

#include "source/ffmpeg_source.h"
#include "source/flv_source.h"
#include "source/inject_source.h"

#ifdef MUXPLAY_ENABLE_FLV
#include "demuxing/flv_demuxing.h"
#include <FlvParser.h>
#endif

extern "C" {
#include <libswscale/swscale.h>
#include <libavutil/opt.h>
}


namespace muxplay {

	int MultiplexPlayer::Interrupt_callback(void* opaque)
	{
		auto ctx = static_cast<MultiplexPlayer*>(opaque);
		if (ctx) {
			if (ctx->mux_private_->is_stop_) {
				LOG_DEBUG("[mux_player] [uuid={}] 当前已中断拉流线程", ctx->mux_private_->uuid_);
				return 1;
			}
			if (ctx->player_status_ == E_PLAYER_STATUS::PLAYER_STATUS_OPENING) {
				int time = ctx->mux_private_->ticker_.elapsedTime();
				if (time > ctx->timeout_) {
					ctx->player_status_ = E_PLAYER_STATUS::PLAYER_STATUS_OPEN_TIMEOUT;
					LOG_WARN("[mux_player] [uuid={}] 打开拉流超时", ctx->mux_private_->uuid_);
					return 1;
				}
			}
			else if (ctx->player_status_ == E_PLAYER_STATUS::PLAYER_STATUS_PLAYING){
				int time = ctx->mux_private_->ticker_.elapsedTime();
				if (time > 3000) {
					LOG_ERR("[mux_player] [uuid={}] 3s内未生成帧,停止视频流", ctx->mux_private_->uuid_);
					ctx->player_status_ = E_PLAYER_STATUS::PLAYER_STATUS_DECODER_ERR;
					return 1;
				}
			}
		}
		else {
			return 1;
		}
		return 0;
	}

	MultiplexPlayer::MultiplexPlayer() 
	{
		mux_private_ = new MultiplexPlayerPrivate();
		mux_private_->rtsp_user_ = "admin";
		mux_private_->rtsp_pwd_ = "admin123";
		auto now = std::chrono::system_clock::now();
		auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
			now.time_since_epoch()
		).count();
		mux_private_->uuid_ = std::to_string(ms) +"_"+ std::to_string(reinterpret_cast<uintptr_t>(this));
	}

	MultiplexPlayer::~MultiplexPlayer()
	{
		if (mux_private_) {
			destryMux(true);
			delete mux_private_;
		}
	}

	void MultiplexPlayer::setRtspAuth(const std::string& user,const std::string& pwd)
	{
		mux_private_->rtsp_user_ = user;
		mux_private_->rtsp_pwd_ = pwd;
	}

	bool MultiplexPlayer::open(const char* url, HWND hwnd, int timeout, void* user_data, PlayerStatusCallback callback, bool enableAudio)
	{
		player_status_ = E_PLAYER_STATUS::PLAYER_STATUS_OPENING;
		if(!mux_private_->openThread_){
			LOG_INFO("[mux_player] [uuid={}] 同步打开流 url={}, timeout={}, enableAudio={}", mux_private_->uuid_, url, timeout, enableAudio);
		}
		mttool::onceToken token(nullptr,[=] {
            if(player_status_ == E_PLAYER_STATUS::PLAYER_STATUS_OPEN_FAILED || player_status_ == E_PLAYER_STATUS::PLAYER_STATUS_OPEN_TIMEOUT 
				|| player_status_ == E_PLAYER_STATUS::PLAYER_STATUS_DECODER_ERR){
				// 先清理所有已分配资源，再通知上层。
				// 否则上层在回调中立即 close()/open() 会与尚未执行的 destryMux 产生竞态。
				mux_private_->is_stop_ = true;
				destryMux(true);
				if (mux_private_->formatContext_) {
					avformat_close_input(&mux_private_->formatContext_);
				}
				// 清理 openThread_ 自身（当前线程即 openThread_，不能 join 但可 detach）。
				// 否则上层不调用 close() 时 openThread_ 残留，后续 openAsync 永久返回 false。
				// ★ 加锁与 close() 互斥，防止 TOCTOU 竞态导致空指针崩溃
				{
					std::lock_guard<std::mutex> lock(mux_private_->openThreadMutex_);
					if (mux_private_->openThread_ && mux_private_->openThread_->joinable()) {
						mux_private_->openThread_->detach();
					}
					mux_private_->openThread_.reset();
				}
				if(mux_private_->statusCallback_){
					mux_private_->statusCallback_(player_status_, mux_private_->user_data_, "open failed");
				}
			}
        });

		source_ = std::make_unique<FFmpegSource>(this);

		IStreamSource::OpenContext ctx;
		ctx.url         = url;
		ctx.hwnd        = hwnd;
		ctx.timeout     = timeout;
		ctx.userData    = user_data;
		ctx.callback    = callback;
		ctx.enableAudio = enableAudio;
		ctx.decodeMode  = decodeMode_;

		return source_->prepare(ctx);
	}

	bool MultiplexPlayer::openAsync(const char* url, HWND hwnd, int timeout, void* user_data, PlayerStatusCallback callback, bool enableAudio)
	{
		LOG_INFO("[mux_player] [uuid={}] 异步打开流 url={}, timeout={}, enableAudio={}", mux_private_->uuid_, url, timeout, enableAudio);
		{
			std::lock_guard<std::mutex> lock(mux_private_->openThreadMutex_);
			if (mux_private_->openThread_) {
				LOG_ERR("[mux_player] [uuid={}] openAsync 拉流线程已存在，上次拉流未释放", mux_private_->uuid_);
				return false;
			}
			mux_private_->is_stop_ = false;
			std::string uri = std::string(url);
			mux_private_->openThread_ = std::make_unique<std::thread>([this,uri, hwnd, timeout, user_data, callback, enableAudio]() {
				this->open(uri.c_str(), hwnd, timeout, user_data, callback, enableAudio);
			});
		}
		return true;
	}

	bool MultiplexPlayer::openFlv(const char* url, HWND hwnd, int timeout, void* user_data, PlayerStatusCallback callback, bool enableAudio)
	{
#ifdef MUXPLAY_ENABLE_FLV
		if (!url || !url[0]) return false;
		player_status_ = E_PLAYER_STATUS::PLAYER_STATUS_OPENING;
		LOG_INFO("[mux_player] [uuid={}] 打开 FLV 流 url={}, timeout={}, enableAudio={}", mux_private_->uuid_, url, timeout, enableAudio);
		source_ = std::make_unique<FlvSource>(this);
		IStreamSource::OpenContext ctx;
		ctx.url         = url;
		ctx.hwnd        = hwnd;
		ctx.timeout     = timeout;
		ctx.userData    = user_data;
		ctx.callback    = callback;
		ctx.enableAudio = enableAudio;
		ctx.decodeMode  = decodeMode_;

		return source_->prepare(ctx);
#else
		LOG_ERR("不支持HTTP-FLV拉流");
		return false;
#endif // MUXPLAY_ENABLE_FLV
	}

	// ── 外部流注入：通过 eventpp 回调接收编码后的 H.264/H.265 包，走 HW 解码+GL零拷贝渲染 ──
	bool MultiplexPlayer::injectExternalStream(event_video_cb* epp_video_cb, HWND hwnd,
		void* user_data, PlayerStatusCallback callback, bool encode_type)
	{
		if (!epp_video_cb) return false;
		player_status_ = E_PLAYER_STATUS::PLAYER_STATUS_OPENING;
		LOG_INFO("[mux_player] [uuid={}] 注入外部流，编码方式 {}", mux_private_->uuid_, encode_type ? "H.265" : "H.264");
		source_ = std::make_unique<InjectSource>(this);

		IStreamSource::OpenContext ctx;
		ctx.extCallback = epp_video_cb;
		ctx.encodeType  = encode_type;
		ctx.hwnd        = hwnd;
		ctx.userData    = user_data;
		ctx.callback    = callback;
		ctx.decodeMode  = decodeMode_;

		return source_->prepare(ctx);
	}

	// ── 公共渲染管线装配（open/openFlv/injectExternalStream 共用）──
	// 前置条件：decoder_ 已 init 成功、video_width_/video_height_ 已填充。
	bool MultiplexPlayer::setupRenderPipeline(HWND hwnd, bool ptsIsMillis, bool liveMode, bool withOverlay)
	{
		// ── 创建 D3D11 设备（HW 解码通常已由解码器创建；SW+D3D 渲染需预先创建）──
		if (!mux_private_->d3dDevice_) {
			ID3D11Device* device = nullptr;
			ID3D11DeviceContext* context = nullptr;
			if (tmcore::CreateD3D11Device(mux_private_->preferGpuIndex_, &device, &context)) {
				mux_private_->d3dDevice_ = device;
				mux_private_->d3dContext_ = context;
			}
		}

		// ── 创建帧消费器（渲染器）──
		// 硬解：必须 D3D11FrameRenderer（D3D11VA Surface 需共享纹理或 SwapChain）
		// 软解+D3D：D3D11FrameRenderer（SwapChain + YUV420P 上传）
		// 软解+GL：FramePump（CPU 帧 → SoftGLDisplay）
		bool needD3DRenderer = (decodeMode_ == DecodeMode::Hardware) ||
		                      (mux_private_->d3dDevice_ && mux_private_->d3dContext_ && !mux_private_->useGLInterop_);
		if (needD3DRenderer) {
			auto renderer = std::make_unique<D3D11FrameRenderer>(hwnd);
			renderer->setPlayerUUid(mux_private_->uuid_);
			if (!renderer->init(mux_private_->d3dDevice_, mux_private_->d3dContext_,
			                    &mux_private_->d3dMutex_,
			                    mux_private_->video_width_, mux_private_->video_height_,
			                    mux_private_->useGLInterop_)) {
				LOG_ERR("[mux_player] [uuid={}] 创建渲染器 D3D11FrameRenderer init failed", mux_private_->uuid_);
				return false;
			}
			consumer_ = std::move(renderer);
		} else {
			consumer_ = std::make_unique<FramePump>();
			consumer_->setPlayerUUid(mux_private_->uuid_);
		}

		// ── 初始化 Overlay 管理器 + PIP（仅带窗口的播放路径）──
		if (withOverlay && mux_private_->d3dDevice_) {
			if (!mux_private_->overlayMgr_) {
				mux_private_->overlayMgr_ = new OverlayManager();
			}
			if (mux_private_->overlayMgr_->initD3D(mux_private_->d3dDevice_)) {
				// ── 高 DPI 适配：从窗口句柄获取 DPI 缩放比 ──
				UINT dpi = hwnd ? ::GetDpiForWindow(hwnd) : 96;
				float dpiScale = dpi / 96.0f;
				mux_private_->overlayMgr_->setDpiScale(dpiScale);

				auto* d3dRenderer = dynamic_cast<D3D11FrameRenderer*>(consumer_.get());
				if (d3dRenderer) {
					d3dRenderer->setOverlayManager(mux_private_->overlayMgr_);
					d3dRenderer->setDpiScale(dpiScale);

					// ── 初始 resize：将 SwapChain 对齐到窗口实际尺寸 ──
					RECT clientRect;
					if (hwnd && ::GetClientRect(hwnd, &clientRect) && clientRect.right > 0 && clientRect.bottom > 0) {
						d3dRenderer->resizeToRect((uint32_t)clientRect.right, (uint32_t)clientRect.bottom);
					}
				}
			}

			// ── 延迟应用 PIP 状态（enablePip/setPipRect 可能在 open 之前调用）──
			applyPendingPipState();
		}

		// 注册帧就绪回调
		consumer_->setOnFrameReady([this]() {
			if (mux_private_->onFrameRendered_) {
				mux_private_->onFrameRendered_();
			}
		});

		// ── 启动解码线程 ──
		mux_private_->decoderThread_ = std::make_unique<std::thread>([this]() {
			this->decoderLoop();
		});

		// ── 初始化 AV 同步架构 ──
		mux_private_->mediaClock_.init();
		mux_private_->mediaClock_.audioPtsCallback = mux_private_->audioClockCallback_;
		mux_private_->mediaClock_.syncToleranceMs = mux_private_->audioSyncToleranceMs_;
		mux_private_->syncParams_.toleranceMs = mux_private_->audioSyncToleranceMs_;

		mux_private_->renderScheduler_ = std::make_unique<RenderScheduler>(
			consumer_.get(),
			&mux_private_->mediaClock_,
			&mux_private_->syncStrategy_,
			&mux_private_->cv_mutex_,
			&mux_private_->frame_cv_);
		mux_private_->renderScheduler_->setUuid(mux_private_->uuid_);

		// Live 模式：无音频流场景，帧到达即渲染
		if (liveMode) {
			mux_private_->renderScheduler_->setLiveMode(true);
		}
		else if(!mux_private_->audioEnabled_) {
			// 音频不解码-->使用 PTS 壁钟直驱模式
			mux_private_->renderScheduler_->setClockMaster(ClockMaster::PTS);
		}

		// 设置 PTS 计算器
		mux_private_->renderScheduler_->setPtsCalculator(
			[this, ptsIsMillis](AVFrame* frame) -> int64_t {
				if (ptsIsMillis) {
					return frame->pts;  // FLV / 注入流：PTS 已是毫秒
				}
#ifdef MUXPLAY_ENABLE_FLV
				if (mux_private_->isFlvStream_) {
					return frame->pts;  // FLV: 已是毫秒
				}
#endif
				if (mux_private_->formatContext_ && mux_private_->videoStreamIndex_ >= 0) {
					AVRational tb = mux_private_->formatContext_->streams[mux_private_->videoStreamIndex_]->time_base;
					return (int64_t)(frame->pts * av_q2d(tb) * 1000);
				}
				return frame->pts;
			});

		// 设置首帧回调
		mux_private_->renderScheduler_->setFirstFrameCallback([this]() {
			if (!mux_private_->firstVideoFrameRendered_.load(std::memory_order_acquire)) {
				mux_private_->firstVideoFrameRendered_.store(true, std::memory_order_release);
				player_status_ = E_PLAYER_STATUS::PLAYER_STATUS_FIRST_FRAME;
				LOG_INFO("[mux_player] [uuid={}] 首帧开始渲染, 音频解锁", mux_private_->uuid_);
				if (mux_private_->statusCallback_) {
					mux_private_->statusCallback_(player_status_, mux_private_->user_data_, "first frame rendered");
				}
			} else if (player_status_ == E_PLAYER_STATUS::PLAYER_STATUS_FIRST_FRAME) {
				LOG_INFO("[mux_player] [uuid={}] 首帧回调，开始播放", mux_private_->uuid_);
				player_status_ = E_PLAYER_STATUS::PLAYER_STATUS_PLAYING;
				if (mux_private_->statusCallback_) {
					mux_private_->statusCallback_(player_status_, mux_private_->user_data_, "playing");
				}
			}
		});

		// 设置预渲染回调（截图 + 录制）
		mux_private_->renderScheduler_->setPreRenderCallback(
			[this](AVFrame* frame) {
				mux_private_->snapshotMgr_.onDecodedFrame(frame);
				mux_private_->recordingMgr_.onDecodedFrame(frame);
			});

		// ── 启动渲染线程 ──
		mux_private_->renderThread_ = std::make_unique<std::thread>([this]() {
			mux_private_->renderScheduler_->run(
				mux_private_->decoded_frame_queue_,
				mux_private_->is_stop_);
		});

		// ── 启动音频解码线程 ──
		if (mux_private_->audioEnabled_ && mux_private_->audioDecoder_) {
			mux_private_->audioDecodeThread_ = std::make_unique<std::thread>([this]() {
				this->audioDecodeLoop();
			});
		}

		return true;
	}

	void MultiplexPlayer::decoderLoop()
	{
		bool firstFrameFlag = false;
		int consecutiveErrorCount = 0;  // 连续解码错误计数（仅 Error 递增）
		int delayedCount = 0;           // 连续延迟计数（仅 Delayed 递增，GotFrame 复位）

		// ★ 流超时检测：首帧后，如果 4s 内没有产出新帧，视为流异常
		//   - 首帧前不计时（避免 HEVC 高延迟编解码器启动缓冲误触发）
		//   - GotFrame 重置计时器；NeedMoreData/Delayed/Error 不重置
		//   - 超时后触发异步 close()，通过回调通知 Closing → Closed
		constexpr int STREAM_TIMEOUT_MS = 4000;
		bool streamTimeout = false;
		bool firstFrameReceived = false;
		auto lastGotFrameTime = std::chrono::steady_clock::now();

		LOG_DEBUG("[mux_player] [uuid={}] 解码线程启动, STREAM_TIMEOUT_MS={}", mux_private_->uuid_, STREAM_TIMEOUT_MS);
		while (!mux_private_->is_stop_) {
			// ★ 首帧后检测流超时
			if (firstFrameReceived && !streamTimeout) {
				auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
					std::chrono::steady_clock::now() - lastGotFrameTime).count();
				if (elapsed > STREAM_TIMEOUT_MS) {
					streamTimeout = true;
					LOG_ERR("[mux_player] [uuid={}] 获取有效解码帧超时,停止播放器 {}ms (> {}ms), "
						"triggering async close", mux_private_->uuid_, (long long)elapsed, STREAM_TIMEOUT_MS);
					mux_private_->is_stop_ = true;
					break;
				}
			}

			DecodedFrame df;
			DecodeResult result;
	
			// D3D11VA 解码器内部使用 d3dContext_，需持锁保护
			if (decodeMode_ == DecodeMode::Hardware) {
				std::lock_guard<std::mutex> lock(mux_private_->d3dMutex_);
				result = decoder_->decode(df);
			} else {
				result = decoder_->decode(df);
			}
	
			switch (result) {
			case DecodeResult::GotFrame:
				consecutiveErrorCount = 0;
				delayedCount = 0;
				firstFrameReceived = true;
				lastGotFrameTime = std::chrono::steady_clock::now();  // ★ 重置流超时计时器
				if (!firstFrameFlag) {
					firstFrameFlag = true;
					LOG_INFO("[mux_player] [uuid={}] first frame: w={} h={} format={} pts={} hw_frames_ctx={:p}", mux_private_->uuid_, df.avframe->width, df.avframe->height, df.avframe->format,
						(long long)df.avframe->pts, (void*)df.avframe->hw_frames_ctx);
					// if (df.avframe->format == AV_PIX_FMT_D3D11 && df.avframe->data[0]) {
					// 	LOG_INFO("[mux_player] [uuid=%s] D3D11 texture ptr=%p subresource=%lld", mux_private_->uuid_.c_str(), (void*)df.avframe->data[0], (long long)(intptr_t)df.avframe->data[1]);
					// }

				}
				// 产出一帧：推入 frame moodycamel 队列
				while (!mux_private_->decoded_frame_queue_.try_enqueue(df.avframe) && !mux_private_->is_stop_) {
					std::unique_lock<std::mutex> lock(mux_private_->cv_mutex_);
					mux_private_->frame_cv_.wait_for(lock, std::chrono::milliseconds(2));
				}
				mux_private_->frame_cv_.notify_one();
				mux_private_->ticker_.resetTime();  // 看门狗：每次成功解码复位
				break;
	
			case DecodeResult::NeedMoreData:
				// 队列空，正常等待
				consecutiveErrorCount = 0;
				{
					std::unique_lock<std::mutex> lock(mux_private_->cv_mutex_);
					mux_private_->packet_cv_.wait_for(lock, std::chrono::milliseconds(10), [&] {
						return mux_private_->vid_packetQueue_.size_approx() > 0 || mux_private_->is_stop_;
					});
				}
				break;
	
			case DecodeResult::Delayed:
				// send_packet 成功但 receive_frame EAGAIN——HEVC 等高延迟编解码器正常行为
				consecutiveErrorCount = 0;
				delayedCount++;
				if (delayedCount == 100) {
					LOG_WARN("[mux_player] [uuid={}] 发生{}次送入packet未获取有效帧 (packet_count={} pending={}). "
						"Decoder may have init issues (extradata/format).",
						mux_private_->uuid_,
						delayedCount,
						mux_private_->vid_packetQueue_.size_approx(),
						(int)decoder_->hasPendingPacket());
				} else if (delayedCount % 500 == 0 && delayedCount > 100) {
					LOG_WARN("[mux_player] [uuid={}] 发生{}次送入packet未获取有效帧 (packet_count={} pending={}). "
						"Decoder may have init issues (extradata/format).",
						mux_private_->uuid_,
						delayedCount,
						mux_private_->vid_packetQueue_.size_approx(),
						(int)decoder_->hasPendingPacket());
				}
				// 短暂等待后重试，避免忙等
				{
					std::unique_lock<std::mutex> lock(mux_private_->cv_mutex_);
					mux_private_->packet_cv_.wait_for(lock, std::chrono::milliseconds(1));
				}
				break;
	
			case DecodeResult::Error:
				consecutiveErrorCount++;
				if (consecutiveErrorCount >= 100) {
					LOG_ERR("[mux_player] [uuid={}] 连续解码错误次数: {} , reporting decoder error", mux_private_->uuid_, consecutiveErrorCount);
					player_status_ = E_PLAYER_STATUS::PLAYER_STATUS_DECODER_ERR;
					if (mux_private_->statusCallback_) {
						mux_private_->statusCallback_(player_status_, mux_private_->user_data_, "decoder error");
					}
					break;
				}
				// 短暂等待后重试
				{
					std::unique_lock<std::mutex> lock(mux_private_->cv_mutex_);
					mux_private_->packet_cv_.wait_for(lock, std::chrono::milliseconds(2));
				}
				break;
			}

			// 致命错误退出
			if (result == DecodeResult::Error && consecutiveErrorCount >= 100) {
				break;
			}
		}

		// ★ 流超时：异步触发 close() 完成清理和状态回调
		//   不能直接调用 close()——它会 join decoderThread_（即当前线程），导致死锁
		//   detached 线程在 decoderThread_ 已退出后执行 close()，安全 join 其他线程
		if (streamTimeout) {
			LOG_WARN("[mux_player] [uuid={}] stream timeout, async close() in detached thread", mux_private_->uuid_);
			std::thread([this]() {
				close();
			}).detach();
		}
	}
	
	void MultiplexPlayer::audioDecodeLoop()
	{
		fprintf(stderr, "[audioDecodeLoop] started, enabled=%d hasDecoder=%d hasCallback=%d\n",
			(int)mux_private_->audioEnabled_,
			(int)(mux_private_->audioDecoder_ != nullptr),
			(int)(mux_private_->audioFrameCallback_ != nullptr));

		// ── 等待视频首帧渲染后才开始解码音频，保证音视频同步启动 ──
		while (!mux_private_->is_stop_ &&
			   !mux_private_->firstVideoFrameRendered_.load(std::memory_order_acquire)) {
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
		}
		if (!mux_private_->is_stop_) {
			fprintf(stderr, "[audioDecodeLoop] video first frame rendered, starting audio decode\n");
		}

		int audioDbgPktCnt = 0;
		while (!mux_private_->is_stop_) {
			PacketWrapper wrapper;
			if (mux_private_->audioPacketQueue_.try_dequeue(wrapper)) {
				audioDbgPktCnt++;
				if (audioDbgPktCnt == 1) {
					fprintf(stderr, "[audioDecodeLoop] first pkt dequeue OK, size=%d\n", wrapper.pkt.size);
				}
				AVPacket& pkt = wrapper.pkt;
	
				// PTS 转换：time_base → 毫秒
				int64_t ptsMs;
#ifdef MUXPLAY_ENABLE_FLV
				if (mux_private_->isFlvStream_) {
					// FLV: PTS 已经是毫秒
					ptsMs = (pkt.pts != AV_NOPTS_VALUE) ? pkt.pts : -1;
				} else
#endif
				{
					ptsMs = (pkt.pts != AV_NOPTS_VALUE && mux_private_->formatContext_ && mux_private_->audioStreamIndex_ >= 0)
						? (int64_t)(pkt.pts * 1000 * av_q2d(mux_private_->formatContext_->streams[mux_private_->audioStreamIndex_]->time_base))
						: -1;
				}
	
				mtAudio::AudioFrame audioFrame;
				int samples = mux_private_->audioDecoder_->decode(pkt.data, pkt.size, audioFrame, ptsMs);
				if (audioDbgPktCnt == 1) {
					fprintf(stderr, "[audioDecodeLoop] first decode: samples=%d ptsMs=%lld sampleRate=%d channels=%d\n",
						samples, (long long)ptsMs,
						audioFrame.sampleRate, audioFrame.channels);
				}
				if (samples > 0) {
					mux_private_->audioDecoderReady_.store(true);
					if (mux_private_->audioFrameCallback_) {
						mux_private_->audioFrameCallback_(audioFrame);
					} else if (audioDbgPktCnt <= 3) {
						fprintf(stderr, "[audioDecodeLoop] WARNING: callback is null, frame dropped (cnt=%d)\n", audioDbgPktCnt);
					}
				} else if (audioDbgPktCnt <= 3) {
					fprintf(stderr, "[audioDecodeLoop] decode returned %d samples (cnt=%d)\n", samples, audioDbgPktCnt);
				}
			} else {
				std::unique_lock<std::mutex> lock(mux_private_->audioCvMutex_);
				mux_private_->audioCv_.wait_for(lock, std::chrono::milliseconds(10), [this] {
					return mux_private_->audioPacketQueue_.size_approx() > 0 || mux_private_->is_stop_;
				});
			}
		}
	}

	void MultiplexPlayer::close()
	{
		if(player_status_ == E_PLAYER_STATUS::PLAYER_STATUS_IDLE){
			LOG_DEBUG("[mux_player] [uuid={}] close() called but player is already idle, skipping", mux_private_->uuid_);
			return;
		}
		LOG_INFO("[mux_player] [uuid={}] 关闭播放器, 当前状态={}", mux_private_->uuid_, (int)player_status_);
		// ★ 防重入：流超时 detached 线程与 UI 线程可能同时调用 close()
		bool expected = false;
		if (!mux_private_->closingInProgress_.compare_exchange_strong(expected, true)) {
			fprintf(stderr, "[close] already in progress, skipping\n");
			return;
		}

		mux_private_->is_stop_ = true;

		// ── 安全等待 openThread_ 结束（与 onceToken 互斥，避免 join/detach 竞态）──
		std::unique_ptr<std::thread> threadToJoin;
		{
			std::lock_guard<std::mutex> lock(mux_private_->openThreadMutex_);
			if (mux_private_->openThread_ && mux_private_->openThread_->joinable() &&
				std::this_thread::get_id() != mux_private_->openThread_->get_id()) {
				threadToJoin = std::move(mux_private_->openThread_);
			} else {
				mux_private_->openThread_.reset();
			}
		}
		// join 在锁外执行，避免死锁（openThread 内的 onceToken 也需要此锁）
		if (threadToJoin) {
			threadToJoin->join();
		}
		// 加固 is_stop_（open() 在其第114行可能将其设为 false）
		mux_private_->is_stop_ = true;

		// 唤醒音频解码线程（确保等待中的 audioCv_ 立即退出）
		{
			std::lock_guard<std::mutex> alock(mux_private_->audioCvMutex_);
			mux_private_->audioCv_.notify_all();
		}
		// 唤醒 decoder/render 线程中可能等待在 packet_cv_ / frame_cv_ 上的线程
		{
			std::lock_guard<std::mutex> cvlock(mux_private_->cv_mutex_);
			mux_private_->packet_cv_.notify_all();
			mux_private_->frame_cv_.notify_all();
		}

		// ── FLV 清理（必须在早返回之前执行，否则 OPENING 状态下 FLV 资源不会被释放）──
#ifdef MUXPLAY_ENABLE_FLV
		// 先关闭 flvDemuxer 设置停止标志，唤醒可能在 seqCv_ 中等待的 flvSetupThread_
		if (mux_private_->flvDemuxer_) {
			fprintf(stderr, "[close] closing flvDemuxer to wake flvSetupThread...\n");
			mux_private_->flvDemuxer_->close();
		}
		// join FLV 初始化线程（flvSetupThread_ 中的 fail() 可能 reset flvDemuxer_，
		// 必须在线程退出后才能安全访问 flvDemuxer_）
		if (mux_private_->flvSetupThread_ && mux_private_->flvSetupThread_->joinable()) {
			fprintf(stderr, "[close] joining flvSetupThread...\n");
			mux_private_->flvSetupThread_->join();
			mux_private_->flvSetupThread_.reset();
		}
		// flvSetupThread_ 已退出，flvDemuxer_ 可能已被 fail() reset，检查后再清理
		if (mux_private_->flvDemuxer_) {
			mux_private_->flvDemuxer_->close();
			mux_private_->flvDemuxer_.reset();
		}
		mux_private_->isFlvStream_ = false;
#endif

		// ── 外部流注入清理：移除 eventpp 回调，阻止编码器线程继续投喂 ──
		if (mux_private_->extStreamCb_) {
			mux_private_->extStreamCb_->remove(mux_private_->extStreamCbHandle_);
			mux_private_->extStreamCb_ = nullptr;
		}
		
		// ── 释放流源（FLV 线程已 join、extCb 回调已移除，可安全销毁）──
		source_.reset();
		
		if(player_status_ != E_PLAYER_STATUS::PLAYER_STATUS_GET_STREAM && player_status_ != E_PLAYER_STATUS::PLAYER_STATUS_FIRST_FRAME && 
			player_status_ != E_PLAYER_STATUS::PLAYER_STATUS_PLAYING){
			mux_private_->closingInProgress_.store(false);
			return;
		}
		player_status_ = E_PLAYER_STATUS::PLAYER_STATUS_CLOSING;
		if (mux_private_->statusCallback_) {
			mux_private_->statusCallback_(player_status_, mux_private_->user_data_, "closing");
		}
		//录制同步关闭（内部持锁 + flush 文件尾部）
		mux_private_->recordingMgr_.stop();

		// 同步等待 decoderThread_ 退出
		if (mux_private_->decoderThread_) {
			if(mux_private_->decoderThread_->joinable()){
				mux_private_->decoderThread_->join();
			}
			mux_private_->decoderThread_.reset();
		}
		// 同步等待 renderThread_ 退出
		if (mux_private_->renderThread_) {
			if(mux_private_->renderThread_->joinable()){
				mux_private_->renderThread_->join();
			}
			mux_private_->renderThread_.reset();
		}
		// 同步等待音频解码线程退出
		if (mux_private_->audioDecodeThread_) {
			if (mux_private_->audioDecodeThread_->joinable()) {
				mux_private_->audioDecodeThread_->join();
			}
			mux_private_->audioDecodeThread_.reset();
		}
		// 清理资源
		if(mux_private_->demuxing_){
			mux_private_->demuxing_->close();
		}
		if (mux_private_->render_) {
			mux_private_->render_->destroy();
		}
		if(mux_private_->codecContext_ ){
			avcodec_free_context(&mux_private_->codecContext_);
		}
		if (mux_private_->formatContext_) {
			avformat_close_input(&mux_private_->formatContext_);
		}
		// FLV 清理已在早返回之前完成
		// 回调放在所有清理完成之后，防止上层在回调中调用 open() 与 close() 竞态
		if (mux_private_->statusCallback_) {
			player_status_ = E_PLAYER_STATUS::PLAYER_STATUS_CLOSED;
			mux_private_->statusCallback_(E_PLAYER_STATUS::PLAYER_STATUS_CLOSED, mux_private_->user_data_, "closed");
		}
		player_status_ = E_PLAYER_STATUS::PLAYER_STATUS_IDLE;
		isPlaying_ = false;
		streamingStarted_ = false;
		mux_private_->firstVideoFrameRendered_.store(false, std::memory_order_release);
		mux_private_->closingInProgress_.store(false);
		LOG_INFO("[mux_player] [uuid={}] 播放器关闭完成, 当前状态={}", mux_private_->uuid_, (int)player_status_);
	}

	void MultiplexPlayer::destryMux(bool is_self)
	{
		// ── 清理截图状态，防止 renderLoop 等待中的 saveSnapshot 悬挂 ──
		mux_private_->snapshotMgr_.abort();

		// ── 清理录制状态，冲洗编码器并关闭文件 ──
		mux_private_->recordingMgr_.stop();

		// ── 清理渲染调度器（持有 consumer_ 裸指针，必须在 consumer_ 销毁前释放）──
		mux_private_->renderScheduler_.reset();

		// ── 清理接口化组件 ──
		decoder_.reset();
		consumer_.reset();

		// ── 清理 Overlay 管理器 ──
		if (mux_private_->overlayMgr_) {
			mux_private_->overlayMgr_->destroy();
			delete mux_private_->overlayMgr_;
			mux_private_->overlayMgr_ = nullptr;
		}

		if (mux_private_->demuxing_){
			mux_private_->demuxing_->close();  // 先停止解封装线程
			delete mux_private_->demuxing_;
			mux_private_->demuxing_ = nullptr;
		}

#ifdef MUXPLAY_ENABLE_FLV
		if (mux_private_->flvDemuxer_) {
			mux_private_->flvDemuxer_->close();
			mux_private_->flvDemuxer_.reset();
		}
#endif
		
		if (mux_private_->render_) {
			mux_private_->render_->destroy();
			delete mux_private_->render_;
			mux_private_->render_ = nullptr;
		}
		if (mux_private_->nv12Share_) {
			mux_private_->nv12Share_->destroy();
			delete mux_private_->nv12Share_;
			mux_private_->nv12Share_ = nullptr;
		}

		if (mux_private_->decoder_) {
			mux_private_->decoder_->close();
			delete mux_private_->decoder_;
			mux_private_->decoder_ = nullptr;
		}

		// 清空 SPSC 队列中的残留数据
		// PacketWrapper - moodycamel 析构会调用 ~PacketWrapper（av_packet_unref），但这里手动清空确保顺序
		{
			PacketWrapper pw;
			while (mux_private_->vid_packetQueue_.try_dequeue(pw)) {}
		}
		// AVFrame* - 需要手动释放
		{
			AVFrame* f;
			while (mux_private_->decoded_frame_queue_.try_dequeue(f)) {
				av_frame_free(&f);
			}
		}

		// 释放解码器上下文（包含 hw_device_ctx 引用）
		if (mux_private_->codecContext_) {
			avcodec_free_context(&mux_private_->codecContext_);
		}

		// 释放共享 D3D11 设备
		if (mux_private_->d3dContext_) {
			mux_private_->d3dContext_->Release();
			mux_private_->d3dContext_ = nullptr;
		}
		if (mux_private_->d3dDevice_) {
			mux_private_->d3dDevice_->Release();
			mux_private_->d3dDevice_ = nullptr;
		}

		// 重置视频流参数
		mux_private_->videoInfo_ = VideoInfo{};

		// ── 清理音频 ──
		mux_private_->audioDecoder_.reset();
		mux_private_->audioInfo_ = AudioInfo{};
		mux_private_->audioDecoderReady_.store(false);
		mux_private_->audioEnabled_ = false;

		// 清空音频包队列
		{
			PacketWrapper pw;
			while (mux_private_->audioPacketQueue_.try_dequeue(pw)) {}
		}

		// ── 外部流注入状态重置 ──
		mux_private_->extStreamCb_ = nullptr;
		mux_private_->extDecoderInited_.store(false, std::memory_order_release);
		mux_private_->isInjectStream_.store(false, std::memory_order_release);
		mux_private_->extPtsCounter_ = 0;

	}

	bool MultiplexPlayer::isPlaying() const
	{
		return player_status_ != E_PLAYER_STATUS::PLAYER_STATUS_IDLE && player_status_ != E_PLAYER_STATUS::PLAYER_STATUS_CLOSED;
	}

	void MultiplexPlayer::replay()
	{

	}
	void MultiplexPlayer::pause()
	{
	
	}

	ID3D11Texture2D* MultiplexPlayer::getSharedTexture() const
	{
		// 从 D3D11FrameRenderer 获取共享纹理
		if (consumer_) {
			auto* renderer = dynamic_cast<D3D11FrameRenderer*>(consumer_.get());
			if (renderer) {
				return renderer->sharedTexture();
			}
		}
		// 兼容旧路径
		if (mux_private_ && mux_private_->nv12Share_) {
			return mux_private_->nv12Share_->sharedTexture();
		}
		if (mux_private_ && mux_private_->render_) {
			return mux_private_->render_->getSharedTexture();
		}
		return nullptr;
	}

	VideoInfo MultiplexPlayer::getVideoInfo() const
	{
		if (mux_private_) {
			return mux_private_->videoInfo_;
		}
		return VideoInfo{};
	}

	void MultiplexPlayer::setGLInteropMode(bool enabled)
	{
		if (mux_private_) {
			mux_private_->useGLInterop_ = enabled;
			if (mux_private_->render_) {
				mux_private_->render_->setGLInteropMode(enabled);
			}
		}
	}

	void MultiplexPlayer::setFrameRenderedCallback(std::function<void()> cb)
	{
		if (mux_private_) {
			mux_private_->onFrameRendered_ = std::move(cb);
		}
	}

	void MultiplexPlayer::lockD3DContext()
	{
		if (mux_private_) {
			mux_private_->d3dMutex_.lock();
		}
	}

	void MultiplexPlayer::unlockD3DContext()
	{
		if (mux_private_) {
			mux_private_->d3dMutex_.unlock();
		}
	}
	
	void MultiplexPlayer::setPreferGPU(int gpuIndex)
	{
		if (mux_private_) {
			mux_private_->preferGpuIndex_ = gpuIndex;
		}
	}
	
	int MultiplexPlayer::preferGPU() const
	{
		return mux_private_ ? mux_private_->preferGpuIndex_ : -1;
	}

	void MultiplexPlayer::setDecodeMode(DecodeMode mode)
	{
		decodeMode_ = mode;
	}

	DecodeMode MultiplexPlayer::decodeMode() const
	{
		return decodeMode_;
	}

	FramePump* MultiplexPlayer::getFramePump() const
	{
		return dynamic_cast<FramePump*>(consumer_.get());
	}

	bool MultiplexPlayer::saveSnapshot(const std::string& filePath)
	{
		if (!mux_private_) return false;
		return mux_private_->snapshotMgr_.save(filePath);
	}

	bool MultiplexPlayer::startRecording(const std::string& filePath)
	{
		if (!mux_private_ || !isPlaying_)
			return false;
		int w = mux_private_->video_width_;
		int h = mux_private_->video_height_;
		if (w <= 0 || h <= 0)
			return false;
		return mux_private_->recordingMgr_.start(filePath, w, h);
	}

	bool MultiplexPlayer::stopRecording()
	{
		return mux_private_ && mux_private_->recordingMgr_.stop();
	}

	bool MultiplexPlayer::isRecording() const
	{
		return mux_private_ && mux_private_->recordingMgr_.isRecording();
	}

	void MultiplexPlayer::resizeRender(int width, int height)
	{
		if (!mux_private_) return;

		// ── 新路径：D3D11FrameRenderer（当前主路径）──
		auto* d3dRenderer = dynamic_cast<D3D11FrameRenderer*>(consumer_.get());
		if (d3dRenderer) {
			std::lock_guard<std::mutex> lock(mux_private_->d3dMutex_);
			d3dRenderer->resizeToRect((uint32_t)width, (uint32_t)height);
			return;
		}

		// ── 旧路径：直接 DxRender（兼容）──
		if (mux_private_->render_) {
			std::lock_guard<std::mutex> lock(mux_private_->d3dMutex_);
			mux_private_->render_->ResizeToRect((uint32_t)width, (uint32_t)height);
		}
	}

	void MultiplexPlayer::setSeiCallback(SeiCallback cb)
	{
		if (mux_private_) {
			mux_private_->seiCallback_ = std::move(cb);
		}
	}

	void MultiplexPlayer::setAudioFrameCallback(AudioFrameCallback cb)
	{
		if (mux_private_) {
			mux_private_->audioFrameCallback_ = std::move(cb);
		}
	}

	void MultiplexPlayer::setAudioClockCallback(AudioClockCallback cb)
	{
		if (mux_private_) {
			mux_private_->audioClockCallback_ = std::move(cb);
		}
	}

	void MultiplexPlayer::setAudioSyncTolerance(int toleranceMs)
	{
		if (mux_private_) {
			mux_private_->audioSyncToleranceMs_ = toleranceMs;
		}
	}

	AudioInfo MultiplexPlayer::getAudioInfo() const
	{
		return mux_private_ ? mux_private_->audioInfo_ : AudioInfo{};
	}

	// ── Overlay API 实现 ──
	int MultiplexPlayer::createTextOverlay(const char* text, const char* fontName, int fontSize)
	{
		if (!mux_private_ || !mux_private_->overlayMgr_) return -1;
		return mux_private_->overlayMgr_->createTextOverlay(text, fontName, fontSize);
	}

	void MultiplexPlayer::destroyTextOverlay(int id)
	{
		if (mux_private_ && mux_private_->overlayMgr_) {
			mux_private_->overlayMgr_->destroyTextOverlay(id);
		}
	}

	bool MultiplexPlayer::updateTextOverlay(int id, const char* text)
	{
		if (!mux_private_ || !mux_private_->overlayMgr_) return false;
		return mux_private_->overlayMgr_->updateTextOverlay(id, text);
	}

	bool MultiplexPlayer::setTextOverlayRect(int id, const RECT& rect)
	{
		if (!mux_private_ || !mux_private_->overlayMgr_) return false;
		return mux_private_->overlayMgr_->setTextOverlayRect(id, rect);
	}

	bool MultiplexPlayer::setTextOverlayColor(int id, unsigned int color)
	{
		if (!mux_private_ || !mux_private_->overlayMgr_) return false;
		return mux_private_->overlayMgr_->setTextOverlayColor(id, color);
	}

	int MultiplexPlayer::createImageOverlay(int width, int height, const void* bgraData, int stride)
	{
		if (!mux_private_ || !mux_private_->overlayMgr_) return -1;
		return mux_private_->overlayMgr_->createImageOverlay(width, height, bgraData, stride);
	}

	void MultiplexPlayer::destroyImageOverlay(int id)
	{
		if (mux_private_ && mux_private_->overlayMgr_) {
			mux_private_->overlayMgr_->destroyImageOverlay(id);
		}
	}

	bool MultiplexPlayer::setImageOverlayRect(int id, const RECT& rect)
	{
		if (!mux_private_ || !mux_private_->overlayMgr_) return false;
		return mux_private_->overlayMgr_->setImageOverlayRect(id, rect);
	}

	bool MultiplexPlayer::setImageOverlayColorTint(int id, unsigned int colorTint)
	{
		if (!mux_private_ || !mux_private_->overlayMgr_) return false;
		return mux_private_->overlayMgr_->setImageOverlayColorTint(id, colorTint);
	}

	// ── PIP 画中画接口 ──
	void MultiplexPlayer::enablePip(bool enabled)
	{
		pendingPipEnabled_ = enabled;
		if (mux_private_) {
			auto* d3dRenderer = dynamic_cast<D3D11FrameRenderer*>(consumer_.get());
			if (d3dRenderer) d3dRenderer->enablePip(enabled);
		}
	}

	bool MultiplexPlayer::isPipEnabled() const
	{
		if (mux_private_) {
			auto* d3dRenderer = dynamic_cast<D3D11FrameRenderer*>(consumer_.get());
			if (d3dRenderer) return d3dRenderer->isPipEnabled();
		}
		return pendingPipEnabled_;
	}

	void MultiplexPlayer::setPipRect(const RECT& rect)
	{
		pendingPipRect_ = rect;
		hasPendingPipRect_ = true;
		if (mux_private_) {
			auto* d3dRenderer = dynamic_cast<D3D11FrameRenderer*>(consumer_.get());
			if (d3dRenderer) d3dRenderer->setPipRect(rect);
		}
	}

	// ── 延迟应用 PIP 状态（在 open() 创建 D3D11FrameRenderer 后调用）──
	void MultiplexPlayer::applyPendingPipState()
	{
		auto* d3dRenderer = dynamic_cast<D3D11FrameRenderer*>(consumer_.get());
		if (!d3dRenderer) return;

		if (pendingPipEnabled_) {
			d3dRenderer->enablePip(true);
		}
		if (hasPendingPipRect_) {
			d3dRenderer->setPipRect(pendingPipRect_);
		}
	}

	// ── DPI 缩放修正 ──
	void MultiplexPlayer::setOverlayDpiScale(float scale)
	{
		if (mux_private_ && mux_private_->overlayMgr_) {
			mux_private_->overlayMgr_->setDpiScale(scale);
		}
		auto* d3dRenderer = dynamic_cast<D3D11FrameRenderer*>(consumer_.get());
		if (d3dRenderer) {
			d3dRenderer->setDpiScale(scale);
		}
	}

};
