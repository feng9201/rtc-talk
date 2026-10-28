#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <Windows.h>
#include <d3d11.h>
#include <functional>
#include <mutex>
#include <string>
#include <chrono>
#include <memory>
#include <eventpp/callbacklist.h>


// ── 音频帧前置声明 ──
namespace mtAudio { struct AudioFrame; }

namespace muxplay {
	enum class E_PLAYER_STATUS {
		PLAYER_STATUS_IDLE = 0,
		PLAYER_STATUS_OPENING,
		PLAYER_STATUS_OPENED,
		PLAYER_STATUS_OPEN_FAILED,
		PLAYER_STATUS_OPEN_TIMEOUT,
		PLAYER_STATUS_GET_STREAM,
		PLAYER_STATUS_FIRST_FRAME,
		PLAYER_STATUS_PLAYING,
		PLAYER_STATUS_DECODER_ERR,
		PLAYER_STATUS_PAUSED,
		PLAYER_STATUS_CLOSING,
		PLAYER_STATUS_CLOSED,   // 资源已全部释放，可安全开始新的 open/openFlv
	};
	using PlayerStatusCallback = std::function<void(E_PLAYER_STATUS,void* user_data,const char* msg)>;

	// ── SEI 数据回调 ──
	// data: SEI NAL unit 数据指针（不含起始码），size: 数据长度，pts: 所属帧的 PTS
	using SeiCallback = std::function<void(const uint8_t* data, size_t size, int64_t pts)>;

	// ── 视频流参数 ──
	struct VideoInfo {
		int         width = 0;        // 视频宽度（像素）
		int         height = 0;       // 视频高度（像素）
		int         bitrate = 0;      // 视频码率（bps），0 表示未知
		float       frameRate = 0.0f; // 帧率（fps），0 表示未知
		std::string codecName;        // 编码格式，如 "h264"、"hevc"
	};

	// ── 音频流参数 ──
	struct AudioInfo {
		int         sampleRate    = 0;
		int         channels      = 0;
		int         bitsPerSample = 0;
		std::string codecName;
	};

	// ── 音频帧回调（在音频解码线程调用）──
	// frame: 解码后的 PCM 帧，包含 PTS 用于音视频同步
	using AudioFrameCallback = std::function<void(const mtAudio::AudioFrame& frame)>;

	// ── 音频播放时钟回调（renderLoop 中调用，用于 AV 同步）──
	// 返回当前音频播放位置 PTS（毫秒），返回 -1 表示无效
	using AudioClockCallback = std::function<int64_t()>;

	// ── 解码模式 ──
	enum class DecodeMode {
		Hardware = 0,  // D3D11VA 硬件解码（默认）
		Software = 1,  // FFmpeg 软件解码
	};

	//head_sps_pps: sps+pps数据，packet:编码后的视频数据，is_key:是否关键帧
	using event_video_cb = eventpp::CallbackList<void(uint8_t* head_sps_pps, int head_size, uint8_t* packet, int packet_size, bool is_key)>;

	class IDecoder;
	class IFrameConsumer;
	class FramePump;
	class MultiplexPlayerPrivate;
	class IStreamSource;
	class FFmpegSource;
	class FlvSource;
	class InjectSource;
	class MultiplexPlayer
	{
	public:
		MultiplexPlayer();
		~MultiplexPlayer();
		// 基于ffmpeg拉流
		bool open(const char* url,HWND hwnd, int timeout=3000,void* user_data=nullptr,PlayerStatusCallback callback=nullptr,bool enableAudio=false);
		bool openAsync(const char* url, HWND hwnd, int timeout = 3000,void* user_data=nullptr,PlayerStatusCallback callback=nullptr,bool enableAudio=false);
		void setRtspAuth(const std::string& user,const std::string& pwd);

		/// HTTP-FLV 拉流（cpr + flvparser），与 open() 平级但走 FLV 解封装路径
		bool openFlv(const char* url, HWND hwnd, int timeout = 5000,
			        void* user_data = nullptr, PlayerStatusCallback callback = nullptr,
			        bool enableAudio = false);

		// 外部注入 encode_type: 0-H264, 1-H265;不支持音频
		bool injectExternalStream(event_video_cb* epp_video_cb,HWND hwnd,void* user_data=nullptr,PlayerStatusCallback callback=nullptr,bool encode_type = 0);
		void close();
		void replay();
		void pause();

		// ── GL 互通接口 ──
		// 获取共享 RGBA 纹理（D3D11_RESOURCE_MISC_SHARED），供 WGL_NV_DX_interop 注册
		ID3D11Texture2D* getSharedTexture() const;
		// 获取视频流参数（分辨率/码率/编码格式/帧率），open 成功后可用
		VideoInfo getVideoInfo() const;
		// 切换渲染目标模式：true=GL互通(NV12Copy+PS→RGBA)，false=SwapChain
		void setGLInteropMode(bool enabled);
		// 设置帧渲染完成回调（跨线程通知 Qt 刷新）
		void setFrameRenderedCallback(std::function<void()> cb);
		
		// ── D3D 上下文线程安全 ──
		// WGL interop 函数（wglDXLockObjectsNV 等）内部会访问 D3D11 ImmediateContext，
		// 与 decoder/render 线程竞争。调用方（GLPlayWidget::paintGL）必须在 WGL 操作前后加锁。
		void lockD3DContext();
		void unlockD3DContext();

		// ── GPU 选择接口（在 open/close 之间调用无效，请在 open 之前调用）──
		// 指定解码+渲染使用的显卡索引（枚举结果中的 index）。-1 恢复默认显卡
		void setPreferGPU(int gpuIndex);
		int preferGPU() const;

		// ── 解码模式 ──
		// 设置解码模式（必须在 open 之前调用）。
		void setDecodeMode(DecodeMode mode);
		DecodeMode decodeMode() const;

		// ── 软解帧访问（仅 Software 模式下有效）──
		// 返回 FramePump 指针，供 GL 显示层获取最新 CPU 帧
		FramePump* getFramePump() const;

		// ── 截图 ──
		// 将当前画面保存为 JPEG 图片。
		// filePath: 保存路径（如 "D:/snapshot.jpg"），目录必须存在。
		// 返回 true 表示保存成功。
		// 注意：硬解模式下内部会做一次 GPU→CPU 传输（av_hwframe_transfer_data），耗时约数毫秒。
		bool saveSnapshot(const std::string& filePath);

		// ── 录制 ──
		// 开始录制：将解码后的视频流保存为 MP4 文件（仅视频，不含音频）。
		// filePath: 保存路径（如 "D:/record.mp4"）。已存在文件将被覆盖。
		// 返回 true 表示录制已启动。注意：open 之后才能调用；重复调用前需先 stopRecording()。
		bool startRecording(const std::string& filePath);
		// 停止录制并写入文件尾部。必须在 close() 之前调用。
		// 返回 true 表示文件写入成功。
		bool stopRecording();
		// 是否正在录制
		bool isRecording() const;

		// ── 渲染控制 ──
		// 供 D3DRenderWidget 在 WM_SIZE 时调用，即时 ResizeBuffers（防闪烁）
		void resizeRender(int width, int height);

		// ── Overlay 接口 ──
		// 在视频画面上叠加文字/图片。overlay ID < 0 表示失败。
		// open 成功后可用，close 后所有 overlay 自动销毁。
		int  createTextOverlay(const char* text, const char* fontName, int fontSize);
		void destroyTextOverlay(int id);
		bool updateTextOverlay(int id, const char* text);
		bool setTextOverlayRect(int id, const RECT& rect);
		bool setTextOverlayColor(int id, unsigned int color); // 0xAARRGGBB

		int  createImageOverlay(int width, int height, const void* bgraData, int stride);
		void destroyImageOverlay(int id);
		bool setImageOverlayRect(int id, const RECT& rect);
		bool setImageOverlayColorTint(int id, unsigned int colorTint); // 0xAARRGGBB

		// ── 画中画（PIP）接口 ──
		// 在 D3D SwapChain 渲染模式下，于指定位置二次绘制同一帧画面
		// rect 为逻辑像素坐标，内部按 DPI 缩放转物理像素
		void enablePip(bool enabled);
		bool isPipEnabled() const;
		void setPipRect(const RECT& rect);

		// ── DPI 缩放修正 ──
		// 修正 Overlay/PIP 的 DPI 缩放比。open() 内部用 GetDpiForWindow 估算，
		// 但 Qt AA_EnableHighDpiScaling 可能未生效（widget 尺寸已是物理像素），
		// 上层可通过比较 GetClientRect(物理) 与 QWidget::width()(逻辑) 计算实际比值。
		void setOverlayDpiScale(float scale);

		// ── SEI 回调 ──
		// 设置 SEI 数据回调。解码器输出帧时，通过 AVFrame side_data 提取
		// H.264/H.265 流中的 SEI user_data_unregistered 数据并传回。
		// 回调在 decoder 线程调用，注意线程安全。
		void setSeiCallback(SeiCallback cb);

		// ── 音频 ──
		// 设置音频帧回调（解码后的 PCM 输出）
		void setAudioFrameCallback(AudioFrameCallback cb);
		// 设置音频播放时钟回调（renderLoop 查询当前音频 PTS）
		void setAudioClockCallback(AudioClockCallback cb);
		// 设置 AV 同步容差（毫秒），默认 200ms
		void setAudioSyncTolerance(int toleranceMs);
		// 获取音频流参数
		AudioInfo getAudioInfo() const;

	public:
		// ── 播放状态 ──
		E_PLAYER_STATUS getPlayStatus() const {return player_status_;};

		// 解码出第一帧后
		bool isRunning() const { return isPlaying_; };
		// 是否正在拉流（open 成功即返回 true，停止/关闭后返回 false）
		bool isStreaming() const { return streamingStarted_; };

		// 开始拉流到完全结束前都在播放中
		bool isPlaying() const;
	private:
		static int Interrupt_callback(void* opaque);
		void decoderLoop();
		void audioDecodeLoop();
		void destryMux(bool is_self=true);

		// ── 延迟应用 PIP 状态（openAsync 时序：enablePip/setPipRect 在 renderer 创建前调用）──
		void applyPendingPipState();

		// ── 公共渲染管线装配（open/openFlv/injectExternalStream 共用）──
		// 创建 D3D 设备/渲染器/（可选）Overlay+PIP/AV 同步调度器，并启动解码、渲染、音频线程。
		// 前置条件：decoder_ 已 init 成功、video_width_/video_height_ 已填充。
		// hwnd        — 渲染目标窗口（GL interop / 注入流可传 nullptr）
		// ptsIsMillis — true: frame->pts 已是毫秒（FLV/注入流）；false: 按流 time_base 换算（ffmpeg）
		// liveMode    — true: Live 渲染模式（帧到即渲，无音频流场景）
		// withOverlay — true: 初始化 OverlayManager 并应用 PIP 延迟状态（注入流传 false）
		bool setupRenderPipeline(HWND hwnd, bool ptsIsMillis, bool liveMode, bool withOverlay);

		// 流源子类直接操作播放器内部状态（decoder_/consumer_/player_status_ 等）
		friend class FFmpegSource;
		friend class FlvSource;
		friend class InjectSource;

	private:
		MultiplexPlayerPrivate* mux_private_;
		bool isPlaying_ = false;
		bool streamingStarted_ = false;
		int  timeout_ = 3000;
		E_PLAYER_STATUS player_status_ = E_PLAYER_STATUS::PLAYER_STATUS_IDLE;
		DecodeMode decodeMode_ = DecodeMode::Hardware;

		// ── 接口化组件 ──
		std::unique_ptr<IDecoder>       decoder_;
		std::unique_ptr<IFrameConsumer> consumer_;
		std::unique_ptr<IStreamSource>  source_;   // 当前流源（FFmpeg/FLV/注入流）

		// ── PIP 延迟状态 ──
		bool pendingPipEnabled_ = false;
		RECT pendingPipRect_ = {0, 0, 0, 0};
		bool hasPendingPipRect_ = false;
	};
};
