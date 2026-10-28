#include "ffmpeg_decoder.h"
#include <w_log_cpp.h>


namespace muxplay
{

	// ── 从 AVFrame side_data 提取 SEI 数据 ──
	// FFmpeg 解码器会自动解析 H.264/H.265 流中的 SEI NAL 单元，
	// 并将 user_data_unregistered 类型的 SEI 存入 AVFrame side_data。
	static void extractSeiFromFrame(const AVFrame* frame, const SeiCallback& cb)
	{
		if (!frame || !cb) return;
		if (frame->sample_rate == 0) {
			return;
		}
		for (int i = 0; i < frame->nb_side_data; i++) {
			const AVFrameSideData* sd = frame->side_data[i];
			if (sd->type == AV_FRAME_DATA_SEI_UNREGISTERED && sd->size > 0) {
				cb(sd->data, (size_t)(sd->size), frame->pts);
			}
		}
	}

	FFmpegDecoder::FFmpegDecoder(MultiplexPlayerPrivate* mux_private)
		:mux_private_(mux_private)
	{
		av_init_packet(&pendingPacket_);
		pendingPacket_.data = nullptr;
		pendingPacket_.size = 0;
	}

	FFmpegDecoder::~FFmpegDecoder()
	{
		if (hasPendingPacket_) {
			av_packet_unref(&pendingPacket_);
		}
	}

	/// 解码一帧，遵循 FFmpeg 推荐的 send/receive 异步模式：
	///   循环执行：
	///     1. 先 drain 解码器内部已产出的帧（receive_frame）
	///     2. 若无帧，取一个 packet 送入解码器（send_packet）
	///     3. send 成功后继续循环，再次 receive_frame
	///     4. send 返回 EAGAIN 时暂存 packet，下次循环优先重发
	///   注意：对于 HEVC 等高延迟编解码器，send 多个 packet 后 receive_frame
	///         才可能产出帧（B帧重排/参考帧建立），这是正常的延迟行为。
	///         返回 Delayed 表示 send 成功但暂无输出帧，调用者应继续送入更多 packet。
	DecodeResult FFmpegDecoder::VideoDecoder(AVFrame* frame)
	{
		if (!frame || !mux_private_ || !mux_private_->codecContext_) {
			return DecodeResult::Error;
		}

		// ────────── 阶段1：尝试从解码器 drain 已产出的帧 ──────────
		int ret = avcodec_receive_frame(mux_private_->codecContext_, frame);
		if (ret >= 0) {
			extractSeiFromFrame(frame, mux_private_->seiCallback_);
			return DecodeResult::GotFrame;  // 成功取出之前 send 的 packet 产出的帧
		}
		if (ret != AVERROR(EAGAIN) && ret != AVERROR_EOF) {
			// 真正的解码错误（非"暂无输出"和"流结束"）
			char errbuf[AV_ERROR_MAX_STRING_SIZE] = {0};
			av_strerror(ret, errbuf, sizeof(errbuf));
			LOG_ERR("[FFmpegDecoder][uuid={}] avcodec_receive_frame error: {} ({})\n", mux_private_->uuid_, ret, errbuf);
			return DecodeResult::Error;
		}
		// ret == EAGAIN：解码器暂无输出，需要送入新 packet

		// ────────── 阶段2：准备 packet（优先使用上次 EAGAIN 暂存的） ──────────
		AVPacket pkt;
		if (hasPendingPacket_) {
			pkt = pendingPacket_;
			// 移交所有权：清空 pendingPacket_ 避免持有悬空引用
			pendingPacket_.data = nullptr;
			pendingPacket_.size = 0;
			hasPendingPacket_ = false;
		} else {
			pkt = mux_private_->dequeueVideoPacket();
			if (pkt.data == nullptr && pkt.size == 0) {
				return DecodeResult::NeedMoreData;  // 队列空，暂无数据
			}
		}

		// ── 首帧关键帧等待：若起始包非 IDR 关键帧，解码器缺少参考帧会导致马赛克或无法解码。
		// 丢弃非关键帧 packet，直到收到第一个关键帧才真正 send。
		// 这对 MP4 文件起始非 IDR 和 FLV 拉流中途切入都很重要。
		if (!firstKeyFrame_) {
			if (!(pkt.flags & AV_PKT_FLAG_KEY)) {
				skipCount_++;
				if (skipCount_ >= 5 && skipCount_ % 5 == 0) {
					LOG_WARN("[FFmpegDecoder][uuid={}] 未收到关键帧，丢弃 (skip={} size={} pts={})", mux_private_->uuid_, skipCount_, pkt.size, (long long)pkt.pts);
				}
				av_packet_unref(&pkt);
				return DecodeResult::Delayed;  // 丢弃非关键帧，不算错误
			}
			firstKeyFrame_ = true;
			skipCount_ = 0;
			LOG_INFO("[FFmpegDecoder][uuid={}] 首个关键帧packet (size={} pts={}), decoding starts",
				mux_private_->uuid_, pkt.size, (long long)pkt.pts);
		}

		// ────────── 阶段3：发送 packet 到解码器 ──────────
		ret = avcodec_send_packet(mux_private_->codecContext_, &pkt);
		if (ret == AVERROR(EAGAIN)) {
			// 解码器输入缓冲区满，暂存 packet 下次重发
			pendingPacket_ = pkt;
			hasPendingPacket_ = true;
			LOG_WARN("[FFmpegDecoder][uuid={}] packet 缓冲区满: ret={}", mux_private_->uuid_, ret);
			return DecodeResult::Delayed;
		}
		if (ret < 0) {
			char errbuf[AV_ERROR_MAX_STRING_SIZE] = {0};
			av_strerror(ret, errbuf, sizeof(errbuf));
			LOG_ERR("[FFmpegDecoder][uuid={}] send packet error: {} ({})  pkt_size={} flags={}\n",
				mux_private_->uuid_, ret, errbuf, pkt.size, pkt.flags);
			av_packet_unref(&pkt);
			return DecodeResult::Error;  // 不可恢复的解码错误
		}
		av_packet_unref(&pkt);  // send 成功，释放 packet

		// ────────── 阶段4：send 成功后尝试取出一帧 ──────────
		// HEVC 等高延迟编解码器在初期可能需要多个 packet 后才产出帧（B帧重排/参考帧建立），
		// receive_frame 返回 EAGAIN 是正常的延迟行为，不是解码错误。
		ret = avcodec_receive_frame(mux_private_->codecContext_, frame);
		if (ret >= 0) {
			extractSeiFromFrame(frame, mux_private_->seiCallback_);
			return DecodeResult::GotFrame;
		} else if (ret != AVERROR(EAGAIN) && ret != AVERROR_EOF) {
			char errbuf[AV_ERROR_MAX_STRING_SIZE] = {0};
			av_strerror(ret, errbuf, sizeof(errbuf));
			LOG_ERR("[FFmpegDecoder][uuid={}] avcodec_receive_frame(after send) error: {} ({})\n", mux_private_->uuid_, ret, errbuf);
			return DecodeResult::Error;
		}
		// EAGAIN: send 成功但暂无输出帧——返回 Delayed 让调用者继续送入更多 packet
		return DecodeResult::Delayed;
	}

	void FFmpegDecoder::close()
	{
		LOG_INFO("[FFmpegDecoder][uuid={}] 解码器对象 close", mux_private_->uuid_);
		if (hasPendingPacket_) {
			av_packet_unref(&pendingPacket_);
			hasPendingPacket_ = false;
		}
		skipCount_ = 0;
		firstKeyFrame_ = false;
	}
}
