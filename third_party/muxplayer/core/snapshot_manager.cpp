#include "snapshot_manager.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
}

#include <fstream>

namespace muxplay {

SnapshotManager::~SnapshotManager()
{
	abort();
	if (frame_) {
		av_frame_free(&frame_);
	}
}

// ── 渲染线程：按需捕获当前帧 ──
void SnapshotManager::onDecodedFrame(AVFrame* frame)
{
	if (!requested_.load(std::memory_order_acquire)) return;

	AVFrame* snap = av_frame_alloc();
	if (snap) {
		if (frame->format == AV_PIX_FMT_D3D11 && frame->hw_frames_ctx) {
			av_hwframe_transfer_data(snap, frame, 0);
		} else {
			av_frame_ref(snap, frame);
		}
		std::lock_guard<std::mutex> lock(mutex_);
		if (frame_) {
			av_frame_free(&frame_);
		}
		frame_ = snap;
		ready_ = true;
	}
	requested_.store(false, std::memory_order_release);
	cv_.notify_one();
}

// ── 调用方线程：请求 + 等待 + 编码 JPEG ──
bool SnapshotManager::save(const std::string& filePath)
{
	{
		std::lock_guard<std::mutex> lock(mutex_);
		ready_ = false;
		aborted_ = false;
	}
	requested_.store(true, std::memory_order_release);

	// 等待渲染线程捕获（最多 3 秒）
	{
		std::unique_lock<std::mutex> lock(mutex_);
		if (!cv_.wait_for(lock, std::chrono::seconds(3), [this] {
			return ready_ || aborted_;
		})) {
			requested_.store(false, std::memory_order_release);
			return false;  // 超时
		}
	}

	if (!frame_) return false;
	return encodeToJpeg(frame_, filePath);
}

void SnapshotManager::abort()
{
	requested_.store(false, std::memory_order_release);
	{
		std::lock_guard<std::mutex> lock(mutex_);
		if (frame_) {
			av_frame_free(&frame_);
		}
		ready_ = true;
		aborted_ = true;
	}
	cv_.notify_all();
}

// ── JPEG 编码辅助 ──
bool SnapshotManager::encodeToJpeg(const AVFrame* srcFrame, const std::string& filePath)
{
	if (!srcFrame || srcFrame->width <= 0 || srcFrame->height <= 0) return false;

	const AVCodec* codec = avcodec_find_encoder(AV_CODEC_ID_MJPEG);
	if (!codec) return false;

	AVCodecContext* encCtx = avcodec_alloc_context3(codec);
	if (!encCtx) return false;

	encCtx->pix_fmt   = AV_PIX_FMT_YUVJ420P;
	encCtx->width     = srcFrame->width;
	encCtx->height    = srcFrame->height;
	encCtx->time_base = {1, 1};
	encCtx->framerate = {1, 1};

	if (avcodec_open2(encCtx, codec, nullptr) < 0) {
		avcodec_free_context(&encCtx);
		return false;
	}

	// 若源帧不是 YUVJ420P，则用 sws_scale 转换
	AVFrame* encFrame = av_frame_alloc();
	encFrame->format = encCtx->pix_fmt;
	encFrame->width  = encCtx->width;
	encFrame->height = encCtx->height;
	av_frame_get_buffer(encFrame, 0);

	SwsContext* sws = nullptr;
	if (srcFrame->format != AV_PIX_FMT_YUVJ420P) {
		sws = sws_getContext(
			srcFrame->width, srcFrame->height, (AVPixelFormat)srcFrame->format,
			encFrame->width, encFrame->height, AV_PIX_FMT_YUVJ420P,
			SWS_BILINEAR, nullptr, nullptr, nullptr);
		if (sws) {
			sws_scale(sws, srcFrame->data, srcFrame->linesize, 0,
			          srcFrame->height, encFrame->data, encFrame->linesize);
			sws_freeContext(sws);
		}
	} else {
		av_frame_copy(encFrame, srcFrame);
	}
	encFrame->pts = 0;

	// 编码
	int ret = avcodec_send_frame(encCtx, encFrame);
	if (ret < 0) {
		av_frame_free(&encFrame);
		avcodec_free_context(&encCtx);
		return false;
	}

	AVPacket* pkt = av_packet_alloc();
	ret = avcodec_receive_packet(encCtx, pkt);
	bool ok = false;
	if (ret >= 0) {
		std::ofstream ofs(filePath, std::ios::binary);
		if (ofs.is_open()) {
			ofs.write(reinterpret_cast<const char*>(pkt->data), pkt->size);
			ok = ofs.good();
		}
	}

	av_packet_free(&pkt);
	av_frame_free(&encFrame);
	avcodec_free_context(&encCtx);
	return ok;
}

} // namespace muxplay
