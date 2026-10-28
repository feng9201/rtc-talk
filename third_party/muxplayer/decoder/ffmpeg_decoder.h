#pragma once
#include "../multiplex_player_private.h"

namespace muxplay
{
	/// 解码结果：区分正常延迟和真正的解码错误
	enum class DecodeResult {
		GotFrame,       // 成功产出一帧
		NeedMoreData,   // 队列空，需要等待更多数据
		Delayed,        // send 成功但 receive EAGAIN（高延迟编解码器正常行为）
		Error           // 真正的解码错误
	};

	class FFmpegDecoder
	{
	public:
		explicit FFmpegDecoder(MultiplexPlayerPrivate* mux_private);
		~FFmpegDecoder();
		/// 解码一帧。返回值指示解码状态，调用者应根据状态类型决定后续行为
		DecodeResult VideoDecoder(AVFrame* frame);
		/// 解码器是否暂存了因 EAGAIN 未能送入的 packet
		bool hasPendingPacket() const { return hasPendingPacket_; }
		/// 是否已收到首帧关键帧
		bool firstKeyFrameReceived() const { return firstKeyFrame_; }
		void close();
	private:
		MultiplexPlayerPrivate* mux_private_ = nullptr;
		// 上一次 send_packet 返回 EAGAIN 时暂存的 packet，下次优先重发
		AVPacket pendingPacket_;
		bool hasPendingPacket_ = false;
		bool firstKeyFrame_ = false;  // 首帧关键帧标志：丢弃关键帧之前的非关键帧 packet
		int  skipCount_ = 0;  // 未收到关键帧时丢弃的非关键帧 packet 计数
	};
};