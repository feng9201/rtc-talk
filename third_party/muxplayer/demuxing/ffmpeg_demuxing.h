#pragma once
#include "../multiplex_player_private.h"


namespace muxplay
{
	class FFmpegDemuxing
	{
	public:
		explicit FFmpegDemuxing(MultiplexPlayerPrivate* mux_private);
		~FFmpegDemuxing();
		
		bool startDemuxing();          // 旧接口：解码器初始化 + 启动 demux 线程
		bool startDemuxingThread();    // 新接口：仅启动 demux 线程（解码器由外部初始化）
		void close();
	private:
		bool InitHWDecoder(const AVCodec* codec, ID3D11Device* sharedDevice);
		static AVPixelFormat GetHWFormat(AVCodecContext* s, const enum AVPixelFormat* fmt);
		void demuxingLoop();
	private:
		MultiplexPlayerPrivate* mux_private_ = nullptr;
		AVBufferRef* hw_device_ctx_ = nullptr;
		int	pix_format_ = -1;
		std::unique_ptr<std::thread> demuxingThread_;

		// 运行时码率/帧率统计
		int64_t bitrateAccumBytes_ = 0;
		int64_t frameCount_ = 0;
		int64_t frameRateFirstPts_ = 0;
		std::chrono::steady_clock::time_point bitrateLastCalcTime_;
	};
};


