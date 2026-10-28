#include "flv_source.h"

#include <cstring>
#include <cstdio>
#include <chrono>
#include <thread>
#include <string>

#include "multiplex_player_private.h"
#include "demuxing/flv_demuxing.h"
#include <FlvParser.h>
#include "decoder/hw_decoder.h"
#include "decoder/sw_decoder.h"
#include <w_log_cpp.h>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
}

namespace muxplay {

FlvSource::~FlvSource() = default;

bool FlvSource::prepare(const OpenContext& ctx)
{
	auto* p = player_->mux_private_;
	hwnd_ = ctx.hwnd;
	liveMode_ = !ctx.enableAudio;

	if (!ctx.url || !ctx.url[0]) return false;

	// ── 先关闭旧 FLV 管线（修复 Wiki #8 竞态）──
	// 关键：flvDemuxer_ 可能在 join 之前为 null（flvSetupThread_ 尚未创建），
	// 但 join 返回后 flvSetupThread_ 已创建并设置了 flvDemuxer_。
	// 此时如果直接 reset() 而不先 close()，demuxLoop 线程仍在运行 → use-after-free 崩溃。
	// 因此：join 后必须再次检查 flvDemuxer_ 并先 close() 再 reset()。
	if (p->flvDemuxer_) {
		p->flvDemuxer_->close();
	}
	if (p->flvSetupThread_ && p->flvSetupThread_->joinable()) {
		LOG_INFO("[openFlv] joining old flvSetupThread...\n");
		p->flvSetupThread_->join();
		LOG_INFO("[openFlv] old flvSetupThread joined\n");
		p->flvSetupThread_.reset();
	}
	// join 后 flvDemuxer_ 可能已被 flvSetupThread_ 设置，必须先 close 再 reset
	// close() 是幂等的，多次调用安全
	if (p->flvDemuxer_) {
		LOG_INFO("[openFlv] closing flvDemuxer (post-join check)...\n");
		p->flvDemuxer_->close();
		p->flvDemuxer_.reset();
		p->isFlvStream_ = false;
		LOG_INFO("[openFlv] old flvDemuxer destroyed\n");
	}

	p->is_stop_ = false;
	std::string uri(ctx.url);

	// 在后台线程执行整个 openFlv 流程
	p->flvSetupThread_ = std::make_unique<std::thread>(
		[this, uri, hwnd = ctx.hwnd, timeout = ctx.timeout,
		     user_data = ctx.userData, callback = ctx.callback,
		     enableAudio = ctx.enableAudio, decodeMode = ctx.decodeMode]() {
			auto* pp = player_->mux_private_;
			pp->statusCallback_ = callback;
			pp->user_data_      = user_data;
			player_->timeout_   = timeout;

			// 失败清理辅助
			auto fail = [this](E_PLAYER_STATUS st, const char* msg) {
				auto* pp = player_->mux_private_;
				player_->player_status_ = st;
#ifdef MUXPLAY_ENABLE_FLV
				pp->isFlvStream_ = false;
				// 只 close 不 reset —— flvDemuxer_ 的生命周期由 close() 统一管理
				// 如果这里 reset() 析构 FlvDemuxer，而 close() 中正在 join demuxLoop，
				// demuxLoop 的 WriteCallback 会访问已释放的 this → use-after-free 崩溃
				if (pp->flvDemuxer_) {
					pp->flvDemuxer_->close();
				}
#endif
				if (pp->statusCallback_) {
					pp->statusCallback_(st, pp->user_data_, msg);
				}
			};

			// ── 停止上一次播放 ──
			pp->is_stop_ = true;
			pp->packet_cv_.notify_all();
			pp->frame_cv_.notify_all();

			if (pp->decoderThread_ && pp->decoderThread_->joinable()) {
				pp->decoderThread_->join();
				pp->decoderThread_.reset();
			}
			if (pp->renderThread_ && pp->renderThread_->joinable()) {
				pp->renderThread_->join();
				pp->renderThread_.reset();
			}
			if (pp->audioDecodeThread_ && pp->audioDecodeThread_->joinable()) {
				pp->audioDecodeThread_->join();
				pp->audioDecodeThread_.reset();
			}

			player_->destryMux(true);
			pp->is_stop_ = false;

			// 重置帧间隔计时
			pp->mediaClock_.lastVideoPtsMs = -1;
			pp->mediaClock_.lastVideoWallTime = std::chrono::steady_clock::now();

			player_->player_status_ = E_PLAYER_STATUS::PLAYER_STATUS_OPENING;
			if (pp->statusCallback_) {
				pp->statusCallback_(player_->player_status_, pp->user_data_, "opening flv");
			}

			// ── 启动 FLV 拉流 ──
			pp->flvDemuxer_ = std::make_unique<FlvDemuxer>(pp);
			pp->isFlvStream_ = true;
			if (!pp->flvDemuxer_->start(uri.c_str(), timeout)) {
				fail(E_PLAYER_STATUS::PLAYER_STATUS_OPEN_TIMEOUT, "flv connect timeout or failed");
				return;
			}

			// ── 获取视频 seq header，确定编码类型 ──
			auto seqHeader = pp->flvDemuxer_->getVideoSeqHeader();
			if (!seqHeader || seqHeader->size == 0) {
				fprintf(stderr, "[openFlv] video seq header is empty\n");
				fail(E_PLAYER_STATUS::PLAYER_STATUS_OPEN_FAILED, "video seq header is empty");
				return;
			}

			AVCodecID avCodecId = AV_CODEC_ID_NONE;
			switch (seqHeader->codec_id) {
				case 7:  avCodecId = AV_CODEC_ID_H264;  break;
				case 12: avCodecId = AV_CODEC_ID_HEVC;  break;
				default:
					fprintf(stderr, "[openFlv] unsupported codec_id=%d\n", seqHeader->codec_id);
					fail(E_PLAYER_STATUS::PLAYER_STATUS_OPEN_FAILED, "unsupported codec");
					return;
			}

			const AVCodec* codec = avcodec_find_decoder(avCodecId);
			if (!codec) {
				LOG_ERR("[openFlv] avcodec_find_decoder failed for codec_id={}\n", (int)avCodecId);
				fail(E_PLAYER_STATUS::PLAYER_STATUS_OPEN_FAILED, "avcodec_find_decoder failed");
				return;
			}

			// ── 手动创建 AVCodecContext（不依赖 AVFormatContext）──
			pp->codecContext_ = avcodec_alloc_context3(codec);
			if (!pp->codecContext_) {
				fail(E_PLAYER_STATUS::PLAYER_STATUS_OPEN_FAILED, "avcodec_alloc_context3 failed");
				return;
			}

			// 设置 extradata（AVCC/HVCC 格式，由 FFmpeg 解码器自行解析）
			pp->codecContext_->extradata_size = static_cast<int>(seqHeader->size);
			pp->codecContext_->extradata =
				(uint8_t*)av_mallocz(static_cast<size_t>(seqHeader->size) + AV_INPUT_BUFFER_PADDING_SIZE);
			memcpy(pp->codecContext_->extradata, seqHeader->data, seqHeader->size);
			fprintf(stderr, "[openFlv] extradata size=%d first 20 bytes: ", (int)seqHeader->size);
			for (int i = 0; i < 20 && i < (int)seqHeader->size; ++i)
				fprintf(stderr, "%02x ", seqHeader->data[i]);
			fprintf(stderr, "\n");

			// ── 初始化解码器（isFlvStream_=true 时 init() 走 FLV 分支）──
			if (decodeMode == DecodeMode::Hardware) {
				auto hwDec = std::make_unique<HardwareDecoder>(pp);
				if (!hwDec->init()) {
					fail(E_PLAYER_STATUS::PLAYER_STATUS_DECODER_ERR, "hw decoder init failed");
					return;
				}
				player_->decoder_ = std::move(hwDec);
			} else {
				auto swDec = std::make_unique<SoftwareDecoder>(pp);
				if (!swDec->init()) {
					fail(E_PLAYER_STATUS::PLAYER_STATUS_DECODER_ERR, "sw decoder init failed");
					return;
				}
				player_->decoder_ = std::move(swDec);
			}

			// ── 分辨率探测安全网：若 avcodec_open2 后 w/h 仍为 0（extradata 中 SPS/PPS 缺失或异常），
			//    从码流第一个视频包（含 SPS/PPS）探测分辨率 ──
			if (pp->codecContext_->width <= 0 || pp->codecContext_->height <= 0) {
				fprintf(stderr, "[openFlv] codecContext w/h=0 after init, probing resolution from bitstream...\n");

				// 等待第一个视频包到达（demuxLoop 已在运行）
				for (int wait = 0; wait < 200 && pp->vid_packetQueue_.size_approx() == 0; ++wait)
					std::this_thread::sleep_for(std::chrono::milliseconds(10));

				PacketWrapper wrapper;
				if (pp->vid_packetQueue_.try_dequeue(wrapper)) {
					AVPacket& probePkt = wrapper.pkt;

					fprintf(stderr, "[openFlv] sending full probe pkt (size=%d) to decoder...\n", probePkt.size);
					int sendRet = avcodec_send_packet(pp->codecContext_, &probePkt);
					fprintf(stderr, "[openFlv] send_packet ret=%d\n", sendRet);
					if (sendRet >= 0) {
						AVFrame* probeFrame = av_frame_alloc();
						for (int i = 0; i < 20; ++i) {
							if (avcodec_receive_frame(pp->codecContext_, probeFrame) >= 0) break;
							std::this_thread::sleep_for(std::chrono::milliseconds(5));
						}
						if (probeFrame->width > 0 && probeFrame->height > 0) {
							pp->codecContext_->width  = probeFrame->width;
							pp->codecContext_->height = probeFrame->height;
							fprintf(stderr, "[openFlv] probed resolution: w=%d h=%d\n", probeFrame->width, probeFrame->height);
							// 将探测帧推入帧队列，作为第一帧渲染（避免首帧丢失）
							pp->decoded_frame_queue_.try_enqueue(probeFrame);
							probeFrame = nullptr;
						}
						if (probeFrame) av_frame_free(&probeFrame);
					}

					// 将原始关键帧重新入队
					pp->enqueueVideoPacket(probePkt);
					av_packet_unref(&probePkt);
				} else {
					fprintf(stderr, "[openFlv] no video packet available for probing\n");
				}
			}

			// ── 采集视频流参数 ──
			{
				pp->video_width_  = pp->codecContext_->width;
				pp->video_height_ = pp->codecContext_->height;
				fprintf(stderr, "[openFlv] after probe: codecContext w=%d h=%d pix_fmt=%d\n",
					pp->codecContext_->width, pp->codecContext_->height, pp->codecContext_->pix_fmt);
				pp->videoInfo_.width     = pp->video_width_;
				pp->videoInfo_.height    = pp->video_height_;
				pp->videoInfo_.bitrate   = pp->codecContext_->bit_rate;
				pp->videoInfo_.frameRate = 0.0f;
				pp->videoInfo_.codecName = avcodec_get_name(avCodecId);
			}

			// ── 音频解码器初始化（FLV 路径）──
			if (enableAudio) {
				pp->audioEnabled_ = true;
				auto audioSeq = pp->flvDemuxer_->getAudioSeqHeader();
				fprintf(stderr, "[openFlv][audio] enableAudio=true, audioSeq=%p, size=%d\n",
					(void*)(audioSeq.get()), audioSeq ? (int)audioSeq->size : -1);
				if (audioSeq && audioSeq->size > 0) {
					fprintf(stderr, "[openFlv][audio] seq codec_id=%d, first 16 bytes: ", audioSeq->codec_id);
					for (int i = 0; i < 16 && i < (int)audioSeq->size; ++i)
						fprintf(stderr, "%02x ", audioSeq->data[i]);
					fprintf(stderr, "\n");
					mtAudio::AudioCodec mtCodec;
					bool codecFound = true;
					switch (audioSeq->codec_id) {
					case 10: mtCodec = mtAudio::AudioCodec::AAC;   pp->audioInfo_.codecName = "AAC";    break;
					case 14: mtCodec = mtAudio::AudioCodec::Opus;  pp->audioInfo_.codecName = "Opus";   break;
					case 7:  mtCodec = mtAudio::AudioCodec::G711A; pp->audioInfo_.codecName = "G.711A"; break;
					case 8:  mtCodec = mtAudio::AudioCodec::G711U; pp->audioInfo_.codecName = "G.711U"; break;
					default:
						fprintf(stderr, "[openFlv] unsupported audio codec_id=%d\n", audioSeq->codec_id);
						codecFound = false;
						break;
					}
					if (codecFound) {
						fprintf(stderr, "[openFlv][audio] calling create(mtCodec=%d)...\n", (int)mtCodec);
						fflush(stderr);
						pp->audioDecoder_ = mtAudio::AudioDecoder::create(mtCodec);
						fprintf(stderr, "[openFlv][audio] create returned %p\n", (void*)pp->audioDecoder_.get());
						fflush(stderr);
						if (pp->audioDecoder_ &&
							pp->audioDecoder_->init(audioSeq->data, (int)audioSeq->size, 0, 0)) {
							pp->audioInfo_.sampleRate    = pp->audioDecoder_->outputSampleRate();
							pp->audioInfo_.channels      = pp->audioDecoder_->outputChannels();
							pp->audioInfo_.bitsPerSample = 16;
							fprintf(stderr, "[openFlv][audio] decoder init OK: %s %dHz %dch\n",
								pp->audioInfo_.codecName.c_str(),
								pp->audioInfo_.sampleRate, pp->audioInfo_.channels);
						} else {
							fprintf(stderr, "[openFlv][audio] decoder init FAILED (decoder=%p)\n",
								(void*)pp->audioDecoder_.get());
							pp->audioDecoder_.reset();
							pp->audioEnabled_ = false;
						}
						fflush(stderr);
					} else {
						pp->audioEnabled_ = false;
					}
				} else {
					fprintf(stderr, "[openFlv][audio] no audio seq header found\n");
					pp->audioEnabled_ = false;
				}
			} else {
				pp->audioEnabled_ = false;
			}

			// ── FLV 音频回调：将音频包转 AVPacket 入队 ──
			fprintf(stderr, "[openFlv][audio] setting up callback: audioEnabled_=%d audioDecoder_=%p\n",
				(int)pp->audioEnabled_, (void*)pp->audioDecoder_.get());
			if (pp->audioEnabled_ && pp->audioDecoder_) {
				pp->flvDemuxer_->setAudioCallback(
					[pp](int64_t pts, const uint8_t* data, uint32_t size, int32_t /*codec_id*/) {
						AVPacket avpkt = {};
						av_new_packet(&avpkt, static_cast<int>(size));
						memcpy(avpkt.data, data, size);
						avpkt.pts = pts;  // FLV PTS 已是毫秒
						avpkt.dts = pts;
						pp->enqueueAudioPacket(avpkt);
						av_packet_unref(&avpkt);
					});
			}

			// ── 装配渲染管线（公共尾部）──
			if (!player_->setupRenderPipeline(hwnd, /*ptsIsMillis=*/true, /*liveMode=*/!enableAudio, /*withOverlay=*/true)) {
				fail(E_PLAYER_STATUS::PLAYER_STATUS_OPEN_FAILED, "renderer init failed");
				return;
			}

			player_->player_status_ = E_PLAYER_STATUS::PLAYER_STATUS_OPENED;
			player_->isPlaying_       = true;
			player_->streamingStarted_ = true;

			if (pp->statusCallback_) {
				pp->statusCallback_(player_->player_status_, pp->user_data_, "flv opened");
			}
		});

	return true;
}

} // namespace muxplay
