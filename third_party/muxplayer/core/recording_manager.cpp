#include "recording_manager.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
}

#include <cstdio>

namespace muxplay {

RecordingManager::~RecordingManager()
{
	closeLocked();  // 内部对空指针安全：未在录制/已 stop 时立即返回
}

bool RecordingManager::start(const std::string& filePath, int width, int height)
{
	std::lock_guard<std::mutex> lock(mutex_);
	if (recording_.load()) return false;  // 已在录制
	if (width <= 0 || height <= 0) return false;

	// 删除旧文件（覆盖写入）
	std::remove(filePath.c_str());

	if (!openLocked(filePath, width, height))
		return false;

	path_ = filePath;
	recording_.store(true, std::memory_order_release);
	return true;
}

bool RecordingManager::stop()
{
	std::lock_guard<std::mutex> lock(mutex_);
	if (!recording_.load()) return false;  // 未在录制

	recording_.store(false, std::memory_order_release);
	return closeLocked();
}

void RecordingManager::onDecodedFrame(AVFrame* frame)
{
	if (!recording_.load(std::memory_order_acquire)) return;
	std::lock_guard<std::mutex> lock(mutex_);
	writeFrameLocked(frame);
}

// ── 初始化 MP4 输出 + H.264 编码器（调用方须已持锁）──
bool RecordingManager::openLocked(const std::string& path, int width, int height)
{
	AVFormatContext* fmtCtx = nullptr;
	if (avformat_alloc_output_context2(&fmtCtx, nullptr, nullptr, path.c_str()) < 0)
		return false;

	const AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_H264);
	if (!codec) {
		avformat_free_context(fmtCtx);
		return false;
	}

	AVStream* stream = avformat_new_stream(fmtCtx, nullptr);
	if (!stream) {
		avformat_free_context(fmtCtx);
		return false;
	}
	stream->id = fmtCtx->nb_streams - 1;

	AVCodecContext* encCtx = avcodec_alloc_context3(codec);
	if (!encCtx) {
		avformat_free_context(fmtCtx);
		return false;
	}

	encCtx->width         = width;
	encCtx->height        = height;
	encCtx->time_base     = AVRational{1, 25};
	encCtx->framerate     = AVRational{25, 1};
	encCtx->pix_fmt       = AV_PIX_FMT_YUV420P;
	encCtx->gop_size      = 12;
	encCtx->max_b_frames  = 0;
	encCtx->bit_rate      = 4000000;

	av_opt_set(encCtx->priv_data, "preset", "ultrafast", 0);
	av_opt_set(encCtx->priv_data, "tune",   "zerolatency", 0);

	if (fmtCtx->oformat->flags & AVFMT_GLOBALHEADER)
		encCtx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

	if (avcodec_open2(encCtx, codec, nullptr) < 0) {
		avcodec_free_context(&encCtx);
		avformat_free_context(fmtCtx);
		return false;
	}

	if (avcodec_parameters_from_context(stream->codecpar, encCtx) < 0) {
		avcodec_free_context(&encCtx);
		avformat_free_context(fmtCtx);
		return false;
	}

	stream->time_base = encCtx->time_base;

	if (avio_open(&fmtCtx->pb, path.c_str(), AVIO_FLAG_WRITE) < 0) {
		avcodec_free_context(&encCtx);
		avformat_free_context(fmtCtx);
		return false;
	}

	if (avformat_write_header(fmtCtx, nullptr) < 0) {
		avio_closep(&fmtCtx->pb);
		avcodec_free_context(&encCtx);
		avformat_free_context(fmtCtx);
		return false;
	}

	// 分配编码帧（YUV420P）
	AVFrame* encFrame = av_frame_alloc();
	encFrame->format = encCtx->pix_fmt;
	encFrame->width  = encCtx->width;
	encFrame->height = encCtx->height;
	av_frame_get_buffer(encFrame, 0);

	fmtCtx_   = fmtCtx;
	encCtx_   = encCtx;
	stream_   = stream;
	encFrame_ = encFrame;
	pts_      = 0;
	return true;
}

// ── 冲洗编码器并关闭文件（调用方须已持锁；空指针安全）──
bool RecordingManager::closeLocked()
{
	if (!fmtCtx_) return false;

	// 冲洗编码器
	if (encCtx_) {
		avcodec_send_frame(encCtx_, nullptr);
		AVPacket* flushPkt = av_packet_alloc();
		int ret;
		while ((ret = avcodec_receive_packet(encCtx_, flushPkt)) >= 0) {
			av_packet_rescale_ts(flushPkt, encCtx_->time_base,
			                     stream_->time_base);
			flushPkt->stream_index = stream_->index;
			av_interleaved_write_frame(fmtCtx_, flushPkt);
			av_packet_unref(flushPkt);
		}
		av_packet_free(&flushPkt);
	}

	av_write_trailer(fmtCtx_);

	if (encCtx_) { avcodec_free_context(&encCtx_); }
	if (fmtCtx_->pb) avio_closep(&fmtCtx_->pb);
	avformat_free_context(fmtCtx_);
	fmtCtx_ = nullptr;

	av_frame_free(&encFrame_);
	av_frame_free(&tmpFrame_);
	if (swsCtx_) { sws_freeContext(swsCtx_); swsCtx_ = nullptr; }
	return true;
}

// ── 将一帧编码并写入 MP4（调用方须已持锁）──
bool RecordingManager::writeFrameLocked(AVFrame* srcFrame)
{
	// Step 1: 获取 CPU 帧（硬解需 GPU→CPU 传输）
	AVFrame* cpuFrame = nullptr;
	if (srcFrame->format == AV_PIX_FMT_D3D11 && srcFrame->hw_frames_ctx) {
		if (!tmpFrame_)
			tmpFrame_ = av_frame_alloc();
		if (av_hwframe_transfer_data(tmpFrame_, srcFrame, 0) < 0)
			return false;
		cpuFrame = tmpFrame_;
	} else {
		cpuFrame = srcFrame;
	}

	// Step 2: 像素格式 → YUV420P
	AVFrame* encFrame = nullptr;
	if (cpuFrame->format != AV_PIX_FMT_YUV420P) {
		// 若源格式/尺寸变化则重建 SwsContext
		if (!swsCtx_ ||
			cpuFrame->width  != swsSrcWidth_ ||
			cpuFrame->height != swsSrcHeight_ ||
			cpuFrame->format != swsSrcFormat_) {
			if (swsCtx_) {
				sws_freeContext(swsCtx_);
				swsCtx_ = nullptr;
			}
			swsCtx_ = sws_getContext(
				cpuFrame->width, cpuFrame->height,
				static_cast<AVPixelFormat>(cpuFrame->format),
				encFrame_->width, encFrame_->height,
				AV_PIX_FMT_YUV420P, SWS_FAST_BILINEAR,
				nullptr, nullptr, nullptr);
			swsSrcWidth_  = cpuFrame->width;
			swsSrcHeight_ = cpuFrame->height;
			swsSrcFormat_ = cpuFrame->format;
		}
		if (swsCtx_) {
			sws_scale(swsCtx_,
			          cpuFrame->data, cpuFrame->linesize, 0, cpuFrame->height,
			          encFrame_->data, encFrame_->linesize);
		}
		encFrame = encFrame_;
	} else {
		encFrame = cpuFrame;
	}

	// Step 3: 设置 PTS 并编码
	encFrame->pts = pts_++;

	if (avcodec_send_frame(encCtx_, encFrame) < 0)
		return false;

	// Step 4: 收取编码包并写入文件
	AVPacket* outPkt = av_packet_alloc();
	int ret;
	while ((ret = avcodec_receive_packet(encCtx_, outPkt)) >= 0) {
		av_packet_rescale_ts(outPkt, encCtx_->time_base,
		                     stream_->time_base);
		outPkt->stream_index = stream_->index;
		av_interleaved_write_frame(fmtCtx_, outPkt);
		av_packet_unref(outPkt);
	}
	av_packet_free(&outPkt);
	return true;
}

} // namespace muxplay
