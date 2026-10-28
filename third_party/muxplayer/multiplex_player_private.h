#pragma once
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <chrono>
#include <mttool/TimeTicker.h>
#include "multiplex_player.h"

#include <spsc/readerwriterqueue.h>
#include <d3d11.h>
#include <mt_audio_decoder.h>

#include "pipeline/media_clock.h"
#include "pipeline/sync_strategy.h"
#include "pipeline/render_scheduler.h"

#include "core/snapshot_manager.h"
#include "core/recording_manager.h"

#ifdef MUXPLAY_ENABLE_FLV
#include "demuxing/flv_demuxing.h"
#endif


extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}


namespace muxplay {

// ── AVPacket 包装器：提供正确的 move 语义（av_packet_move_ref 转移所有权） ──
struct PacketWrapper {
	AVPacket pkt;
	PacketWrapper() {
		av_init_packet(&pkt);
		pkt.data = nullptr;
		pkt.size = 0;
	}
	~PacketWrapper() { av_packet_unref(&pkt); }
	// 移动构造：使用 av_packet_move_ref 转移 buf 所有权
	PacketWrapper(PacketWrapper&& other) noexcept {
		av_packet_move_ref(&pkt, &other.pkt);
	}
	// 移动赋值
	PacketWrapper& operator=(PacketWrapper&& other) noexcept {
		if (this != &other) {
			av_packet_unref(&pkt);
			av_packet_move_ref(&pkt, &other.pkt);
		}
		return *this;
	}
	// 禁止拷贝
	PacketWrapper(const PacketWrapper&) = delete;
	PacketWrapper& operator=(const PacketWrapper&) = delete;
};

	class FFmpegDemuxing;
	class FFmpegDecoder;
	class DxRender;
	class Nv12TextureShare;
	class RenderScheduler;
	class OverlayManager;
#ifdef MUXPLAY_ENABLE_FLV
	class FlvDemuxer;
#endif
	class MultiplexPlayerPrivate
	{
	public:
		MultiplexPlayerPrivate()
			: vid_packetQueue_(50)   // 初始预分配 ~50 槽
			, decoded_frame_queue_(10) // 初始预分配 ~10 槽（不超过 D3D11 surface pool，避免 get_buffer 失败）
		{}
		~MultiplexPlayerPrivate() {
			// moodycamel 析构时会自动销毁 PacketWrapper（调用 av_packet_unref）
			// 但 AVFrame* 指针需要手动清理
			AVFrame* f;
			while (decoded_frame_queue_.try_dequeue(f)) {
				av_frame_free(&f);
			}
			// snapshotMgr_/recordingMgr_ 由各自析构函数自动清理
		};
	public:
		void enqueueVideoPacket(const AVPacket& packet) {
			PacketWrapper wrapper;
			av_packet_ref(&wrapper.pkt, &packet);
			// moodycamel try_enqueue 不分配内存，满时返回 false
			while (!vid_packetQueue_.try_enqueue(std::move(wrapper))) {
				if (is_stop_) return;
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
			}
			packet_cv_.notify_one();
		}
		AVPacket dequeueVideoPacket() {
			AVPacket pkt{};
			PacketWrapper wrapper;
			if (vid_packetQueue_.try_dequeue(wrapper)) {
				av_packet_move_ref(&pkt, &wrapper.pkt);
			}
			return pkt;
		}

		size_t getVideoPacketQueueSize() const {
			return vid_packetQueue_.size_approx();
		}

		size_t getDecodedFrameQueueSize() const {
			return decoded_frame_queue_.size_approx();
		}
	
		void enqueueAudioPacket(const AVPacket& packet) {
			PacketWrapper wrapper;
			av_packet_ref(&wrapper.pkt, &packet);
			while (!audioPacketQueue_.try_enqueue(std::move(wrapper))) {
				if (is_stop_) return;
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
			}
			audioCv_.notify_one();
		}
	public:
		AVFormatContext*	formatContext_ = nullptr;
		AVCodecContext*		codecContext_ = nullptr;
		int					videoStreamIndex_ = -1;
		int					audioStreamIndex_ = -1;
		bool				audioEnabled_ = false;
		int					video_width_ = 0;
		int					video_height_ = 0;
		ID3D11Device*		d3dDevice_ = nullptr;
		ID3D11DeviceContext* d3dContext_ = nullptr;
		std::mutex			d3dMutex_;	// 保护 d3dContext_ 的线程安全访问（decoder/render 线程竞争）
		std::mutex			openThreadMutex_;	// 保护 openThread_ 的创建/销毁，防止 close() 与 onceToken 竞态
		std::unique_ptr<std::thread> openThread_;
		std::unique_ptr<std::thread> decoderThread_;
		std::unique_ptr<std::thread> renderThread_;
		mttool::Ticker			ticker_;
		std::atomic<bool>		is_stop_ = false;
		std::atomic<bool>		closingInProgress_{false};  // close() 防重入（detached 超时线程与 UI 可能并发）
		std::string             uuid_;  // 用于日志标识不同实例
		//rtsp Authorization
		std::string             rtsp_user_;
		std::string             rtsp_pwd_;

		// moodycamel SPSC 无锁队列（try_enqueue 不扩容，保持有界队列语义）
		// 注意：moodycamel 需要初始容量 = 期望槽数 + 1（含 spare slot）
		moodycamel::ReaderWriterQueue<PacketWrapper>	vid_packetQueue_;
		moodycamel::ReaderWriterQueue<AVFrame*>			decoded_frame_queue_;

		// 条件变量：用于线程间信号通知，避免忙等
		std::mutex				cv_mutex_;
		std::condition_variable	packet_cv_;
		std::condition_variable	frame_cv_;

		// ── 音频解码 ──
		moodycamel::ReaderWriterQueue<PacketWrapper>	audioPacketQueue_{128};
		std::mutex										audioCvMutex_;
		std::condition_variable							audioCv_;

		FFmpegDemuxing*			demuxing_ = nullptr;
		FFmpegDecoder*			decoder_ = nullptr;
		DxRender*				render_ = nullptr;
		Nv12TextureShare*		nv12Share_ = nullptr;  // GL 互通模式（替代 render_）
		bool					useGLInterop_ = false;
		int                     preferGpuIndex_ = -1;  // 用户指定的显卡索引（-1=默认）

		PlayerStatusCallback    statusCallback_ = nullptr;
		void*                   user_data_ = nullptr;

		// ── SEI 回调 ──
		SeiCallback             seiCallback_;

		// 帧渲染完成回调（用于通知 Qt GL widget 刷新）
		std::function<void()>   onFrameRendered_;

		// 视频流参数（在 open 成功后填充）
		VideoInfo               videoInfo_;

		// ── 截图 / 录制管理器 ──
		SnapshotManager         snapshotMgr_;
		RecordingManager        recordingMgr_;

		// ── 音频解码 ──
		std::unique_ptr<mtAudio::AudioDecoder>  audioDecoder_;
		std::unique_ptr<std::thread>            audioDecodeThread_;
		AudioInfo                               audioInfo_;
		std::atomic<bool>                       audioDecoderReady_{false};
		std::atomic<bool>                       firstVideoFrameRendered_{false}; // 视频首帧渲染后才开始音频播放
		AudioFrameCallback                      audioFrameCallback_;
		AudioClockCallback                      audioClockCallback_;
		int                                     audioSyncToleranceMs_ = 200;

		// ── AV 同步架构（三层模型）──
		MediaClock                              mediaClock_;
		SyncParams                              syncParams_;
		SyncStrategy                            syncStrategy_{syncParams_};
		std::unique_ptr<RenderScheduler>        renderScheduler_;

		// ── Overlay ──
		OverlayManager*                         overlayMgr_ = nullptr;

#ifdef MUXPLAY_ENABLE_FLV
		// ── FLV 拉流 ──
		std::unique_ptr<std::thread> flvSetupThread_;  // FLV 管线设置线程（替代 detach，可 join）
		std::unique_ptr<FlvDemuxer> flvDemuxer_;
		bool isFlvStream_ = false;
#endif

		// ── 外部流注入 ──
		event_video_cb*                    extStreamCb_ = nullptr;       // 外部回调列表指针（非拥有）
		event_video_cb::Handle             extStreamCbHandle_;           // 回调句柄（用于移除）
		std::mutex                         extInitMutex_;                // 保护懒初始化
		std::atomic<bool>                  extDecoderInited_{false};     // 解码器是否已初始化
		std::atomic<bool>                  isInjectStream_{false};       // 是否为外部注入模式
		int64_t                            extPtsCounter_ = 0;           // PTS 计数器（毫秒）
	};
};
