#include "ffmpeg_source.h"
#include "multiplex_player_private.h"
#include "demuxing/ffmpeg_demuxing.h"
#include "decoder/hw_decoder.h"
#include "decoder/sw_decoder.h"
#include <w_log_cpp.h>

#include <cstring>
#include <cstdio>
#include <string>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/opt.h>
}

namespace muxplay {

FFmpegSource::~FFmpegSource() = default;

bool FFmpegSource::prepare(const OpenContext& ctx)
{
	auto* p = player_->mux_private_;
	hwnd_ = ctx.hwnd;

	p->statusCallback_ = ctx.callback;
	p->user_data_ = ctx.userData;
	player_->timeout_ = ctx.timeout;
	avformat_network_init();
	p->ticker_.resetCrateTime();
	// ★ 限制 find_stream_info 探测开销
	// 本地文件：默认参数（probesize=5MB, max_analyze_duration=5s），确保关键帧不丢失
	// 网络流：低延迟参数
	bool isLocalFile = (ctx.url && ctx.url[0] != '\0' &&
		strncmp(ctx.url, "http://", 7) != 0 &&
		strncmp(ctx.url, "https://", 8) != 0 &&
		strncmp(ctx.url, "rtsp://", 7) != 0 &&
		strncmp(ctx.url, "rtmp://", 7) != 0 &&
		strncmp(ctx.url, "rtp://", 6) != 0 &&
		strncmp(ctx.url, "udp://", 6) != 0 &&
		strncmp(ctx.url, "tcp://", 6) != 0);

	player_->player_status_ = E_PLAYER_STATUS::PLAYER_STATUS_OPENING;
	if (p->statusCallback_) {
		LOG_INFO("[source] [uuid={}] 状态回调: 视频流打开中", p->uuid_);
		p->statusCallback_(player_->player_status_, p->user_data_, "opening");
	}

	// 单次打开：分配并配置 formatContext，再 avformat_open_input。
	// 失败时 avformat_open_input 会释放并置空 formatContext_，因此每次调用都重新初始化。
	auto openWithUrl = [&](const char* url) -> int {
		p->formatContext_ = avformat_alloc_context();
		if (!p->formatContext_) {
			LOG_ERR("[source] [uuid={}] avformat_alloc_context 失败", p->uuid_);
			return AVERROR(ENOMEM);
		}
		p->formatContext_->interrupt_callback.callback = &MultiplexPlayer::Interrupt_callback;
		p->formatContext_->interrupt_callback.opaque = player_;
		if (!isLocalFile) {
			p->formatContext_->probesize           = 500 * 1024;        // 500KB
			p->formatContext_->max_analyze_duration = 2 * AV_TIME_BASE;  // 2 秒
			p->formatContext_->flags |= AVFMT_FLAG_NOBUFFER;          // 低延迟模式
		}
		p->formatContext_->fps_probe_size = 0;  // 跳过帧率探测

		// RTSP 流：TCP 传输 + socket 超时（timeout 单位微秒；stimeout 仅对 tcp:// 协议有效）
		// 注意：FFmpeg RTSP 不提供 rtsp_user/rtsp_pwd 选项，认证凭证只能内嵌在 URL
		// （rtsp://user:pwd@host/...）中。
		AVDictionary* opts = nullptr;
		bool isRtsp = (ctx.url && strncmp(ctx.url, "rtsp://", 7) == 0);
		if (isRtsp) {
			av_dict_set(&opts, "rtsp_transport", "tcp", 0);
			av_dict_set(&opts, "timeout", "3000000", 0);  // RTSP socket 超时 3s (微秒)
		}
		int ret = avformat_open_input(&p->formatContext_, url, nullptr, &opts);
		av_dict_free(&opts);
		return ret;
	};

	// 对未携带凭证的 RTSP 流：先无凭证打开（兼容无需认证的公开流，如 NVR live_stream），
	// 失败后再带默认凭证重试一次（私有流需要 401 认证）。
	// 直接给公开流注入凭证会被部分服务器拒绝，因此不能无条件注入。
	int openRet = 0;
	std::string authUrl;
	bool canRetryAuth = false;
	if (ctx.url && strncmp(ctx.url, "rtsp://", 7) == 0) {
		const char* hostStart = ctx.url + 7;
		const char* p1 = hostStart;
		while (*p1 && *p1 != '/' && *p1 != '?' && *p1 != '#') p1++;
		std::string authority(hostStart, p1 - hostStart);
		if (authority.find('@') == std::string::npos) {
			authUrl = ctx.url;
			authUrl.insert(authUrl.find("://") + 3, p->rtsp_user_ + ":" + p->rtsp_pwd_ + "@");
			canRetryAuth = true;
			LOG_INFO("[source] [uuid={}] RTSP 未携带凭证，将按需重试认证", p->uuid_);
		}
	}

	openRet = openWithUrl(ctx.url);
	if (openRet != 0 && canRetryAuth) {
		p->ticker_.resetCrateTime();  // 给重试完整的打开超时
		LOG_WARN("[source] [uuid={}] 无凭证打开 RTSP 失败 ret={}, 尝试带凭证重试", p->uuid_, openRet);
		openRet = openWithUrl(authUrl.c_str());
	}
	if (openRet != 0) {
		player_->player_status_ = E_PLAYER_STATUS::PLAYER_STATUS_OPEN_FAILED;
		LOG_ERR("[source] [uuid={}] 打开流失败 ret={}", p->uuid_, openRet);
		return false;
	}

	// 获取视频流信息
	if (avformat_find_stream_info(p->formatContext_, nullptr) < 0) {
		player_->player_status_ = E_PLAYER_STATUS::PLAYER_STATUS_OPEN_FAILED;
		LOG_ERR("[source] [uuid={}] 获取流信息失败", p->uuid_);
		return false;
	}

	// ★ 重置流索引，防止上次 open 残留导致越界
	p->videoStreamIndex_ = -1;
	p->audioStreamIndex_ = -1;
	for (unsigned int i = 0; i < p->formatContext_->nb_streams; i++) {
		if (p->formatContext_->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
			p->videoStreamIndex_ = i;
			continue;
		}
		if (p->formatContext_->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
			p->audioStreamIndex_ = i;
			continue;
		}
	}
	if (p->videoStreamIndex_ == -1) {
		LOG_ERR("[source] [uuid={}] 未找到视频流", p->uuid_);
		player_->player_status_ = E_PLAYER_STATUS::PLAYER_STATUS_OPEN_FAILED;
		return false;
	}

	LOG_DEBUG("[source] [uuid={}] 视频流索引={}, 音频流索引={},开始停止旧会话线程", p->uuid_, p->videoStreamIndex_, p->audioStreamIndex_);
	// 停止旧会话的线程，防止竞态条件
	p->is_stop_ = true;
	if (p->decoderThread_ && p->decoderThread_->joinable()) {
		p->decoderThread_->join();
		p->decoderThread_.reset();
	}
	if (p->renderThread_ && p->renderThread_->joinable()) {
		p->renderThread_->join();
		p->renderThread_.reset();
	}
	if (p->audioDecodeThread_ && p->audioDecodeThread_->joinable()) {
		p->audioDecodeThread_->join();
		p->audioDecodeThread_.reset();
	}
	player_->destryMux(true);
	p->is_stop_ = false;

	// 音频流探测与解码器初始化
	if (ctx.enableAudio && p->audioStreamIndex_ >= 0) {
		LOG_DEBUG("[source] [uuid={}] 音频流索引={}, 开始初始化音频解码器", p->uuid_, p->audioStreamIndex_);
		p->audioEnabled_ = true;
		AVCodecParameters* audioPar = p->formatContext_->streams[p->audioStreamIndex_]->codecpar;

		mtAudio::AudioCodec codecType{};
		const uint8_t* extraData = audioPar->extradata;
		int extraLen = audioPar->extradata_size;

		switch (audioPar->codec_id) {
		case AV_CODEC_ID_AAC:       codecType = mtAudio::AudioCodec::AAC;   p->audioInfo_.codecName = "AAC";    break;
		case AV_CODEC_ID_OPUS:      codecType = mtAudio::AudioCodec::Opus;  p->audioInfo_.codecName = "Opus";   break;
		case AV_CODEC_ID_PCM_ALAW:  codecType = mtAudio::AudioCodec::G711A; p->audioInfo_.codecName = "G.711A"; break;
		case AV_CODEC_ID_PCM_MULAW: codecType = mtAudio::AudioCodec::G711U; p->audioInfo_.codecName = "G.711U"; break;
		default:
			LOG_ERR("[source] [uuid={}] 不支持的音频编码类型: {}", p->uuid_, (int)audioPar->codec_id);
			p->audioEnabled_ = false;
			p->audioStreamIndex_ = -1;
			break;
		}

		if (p->audioEnabled_) {
			LOG_INFO("[source] [uuid={}] 初始化音频解码器 codec={}, sampleRate={}, channels={}, extraLen={}",
				p->uuid_, p->audioInfo_.codecName, audioPar->sample_rate, audioPar->ch_layout.nb_channels, extraLen);
			p->audioDecoder_ = mtAudio::AudioDecoder::create(codecType);
			if (p->audioDecoder_ &&
				p->audioDecoder_->init(extraData, extraLen,
					audioPar->sample_rate, audioPar->ch_layout.nb_channels)) {
				p->audioInfo_.sampleRate    = p->audioDecoder_->outputSampleRate();
				p->audioInfo_.channels      = p->audioDecoder_->outputChannels();
				p->audioInfo_.bitsPerSample = 16;
			} else {
				p->audioDecoder_.reset();
				p->audioEnabled_ = false;
				p->audioStreamIndex_ = -1;
			}
		}
	}

	// 重置帧间隔计时
	p->mediaClock_.lastVideoPtsMs = -1;
	p->mediaClock_.lastVideoWallTime = std::chrono::steady_clock::now();

	// 接口化解码器初始化
	if (ctx.decodeMode == DecodeMode::Hardware) {
		auto hwDec = std::make_unique<HardwareDecoder>(p);
		if (!hwDec->init()) {
			player_->player_status_ = E_PLAYER_STATUS::PLAYER_STATUS_DECODER_ERR;
			LOG_ERR("[source] [uuid={}] 硬件解码器初始化失败", p->uuid_);
			return false;
		}
		player_->decoder_ = std::move(hwDec);
	} else {
		auto swDec = std::make_unique<SoftwareDecoder>(p);
		if (!swDec->init()) {
			player_->player_status_ = E_PLAYER_STATUS::PLAYER_STATUS_DECODER_ERR;
			LOG_ERR("[source] [uuid={}] 软件解码器初始化失败", p->uuid_);
			return false;
		}
		player_->decoder_ = std::move(swDec);
	}
	LOG_INFO("[source] [uuid={}] {}解码器初始化成功", p->uuid_, ctx.decodeMode == DecodeMode::Hardware ? "硬件" : "软件");

	// 采集视频流参数
	{
		LOG_INFO("[source] [uuid={}] 视频流参数: width={}, height={}, bitrate={}, frameRate={:.2f}, codec={}",
			p->uuid_, p->video_width_, p->video_height_, (long long)p->codecContext_->bit_rate,
			(float)p->formatContext_->streams[p->videoStreamIndex_]->avg_frame_rate.num /
			(p->formatContext_->streams[p->videoStreamIndex_]->avg_frame_rate.den > 0 ?
				(float)p->formatContext_->streams[p->videoStreamIndex_]->avg_frame_rate.den : 1.0f),
			avcodec_get_name(p->codecContext_->codec_id));
		
		p->videoInfo_.width  = p->video_width_;
		p->videoInfo_.height = p->video_height_;
		p->videoInfo_.bitrate = p->codecContext_->bit_rate;
		AVStream* stream = p->formatContext_->streams[p->videoStreamIndex_];
		if (p->videoInfo_.bitrate <= 0 && stream->codecpar->bit_rate > 0) {
			p->videoInfo_.bitrate = stream->codecpar->bit_rate;
		}
		if (p->videoInfo_.bitrate <= 0) {
			p->videoInfo_.bitrate = p->formatContext_->bit_rate;
		}
		p->videoInfo_.frameRate = (float)stream->avg_frame_rate.num /
			(stream->avg_frame_rate.den > 0 ? (float)stream->avg_frame_rate.den : 1.0f);
		p->videoInfo_.codecName = avcodec_get_name(p->codecContext_->codec_id);
	}

	// 启动 demux 线程（本地文件 seek 回起始，防止 find_stream_info 消耗起始关键帧）
	if (isLocalFile && p->videoStreamIndex_ >= 0) {
		avformat_flush(p->formatContext_);
		av_seek_frame(p->formatContext_, p->videoStreamIndex_, 0, AVSEEK_FLAG_BACKWARD);
	}
	if (!p->demuxing_) {
		p->demuxing_ = new FFmpegDemuxing(p);
	}
	p->demuxing_->startDemuxingThread();

	// 装配渲染管线（公共尾部：D3D 设备/渲染器/Overlay/调度器/线程）
	if (!player_->setupRenderPipeline(hwnd_, /*ptsIsMillis=*/false, /*liveMode=*/false, /*withOverlay=*/true)) {
		player_->player_status_ = E_PLAYER_STATUS::PLAYER_STATUS_OPEN_FAILED;
		LOG_ERR("[source] [uuid={}] 渲染管线初始化失败", p->uuid_);
		return false;
	}

	player_->player_status_ = E_PLAYER_STATUS::PLAYER_STATUS_OPENED;
	if (p->statusCallback_) {
		p->statusCallback_(player_->player_status_, p->user_data_, "ffpeg demux open");
	}
	player_->streamingStarted_ = true;
	player_->isPlaying_ = true;
	return true;
}

} // namespace muxplay
