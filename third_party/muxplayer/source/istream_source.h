#pragma once
#include "multiplex_player.h"

struct HWND__;

namespace muxplay {

// ── 流源抽象：封装三种拉流路径（FFmpeg / FLV / 注入流）的差异化前端 ──
//
// 职责边界：
//   - 打开流（avformat_open_input / cpr+flvparser / eventpp 回调注册）
//   - 创建 codecContext_、extradata、解码器 decoder_
//   - 探测分辨率、填充 video_width_/height_/videoInfo_
//   - 初始化音频解码器（如启用）
//   - 启动 demux 线程 / 注册数据投递回调
//
// 不负责：渲染管线装配（由 MultiplexPlayer::setupRenderPipeline 统一完成，
//         source 在 decoder/尺寸就绪后调用它）
//
// 生命周期由 MultiplexPlayer 持有（std::unique_ptr<IStreamSource> source_），
// close()/destryMux() 释放前会先 join 各工作线程，故 source 析构时线程已停止。
class IStreamSource
{
public:
	struct OpenContext
	{
		// ── 通用 ──
		const char*           url        = nullptr;   // FLV/FFmpeg 用
		HWND                  hwnd       = nullptr;
		int                   timeout    = 3000;
		void*                 userData   = nullptr;
		PlayerStatusCallback  callback  = nullptr;
		bool                  enableAudio = false;
		DecodeMode            decodeMode  = DecodeMode::Hardware;

		// ── 注入流专用 ──
		event_video_cb*       extCallback = nullptr;  // eventpp 回调对象指针
		bool                  encodeType  = false;       // false=H264, true=H265
	};

	virtual ~IStreamSource() = default;

	// 准备流源：
	//   - FFmpeg/FLV：同步完成（FLV 在后台线程装配，prepare 立即返回）
	//   - 注入流：仅完成 codecContext 创建 + 回调注册，真正的解码器初始化
	//     在首个关键帧到达时由 source 内部触发（调用 setupRenderPipeline）
	// 失败时内部已发状态回调并清理，返回 false。
	virtual bool prepare(const OpenContext& ctx) = 0;

	// 元信息：渲染管线装配参数（供调试/查询，装配由 source 内部直接调 setupRenderPipeline）
	virtual bool ptsIsMilliseconds() const = 0;
	virtual bool liveMode()        const = 0;
	virtual bool withOverlay()     const = 0;
	virtual HWND hwnd()            const = 0;
};

} // namespace muxplay
