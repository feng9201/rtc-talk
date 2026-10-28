#pragma once
#include "istream_source.h"

namespace muxplay {

class MultiplexPlayer;

// ── FFmpeg 拉流源：avformat_open_input / find_stream_info / 音频探测 / demux 线程 ──
// 对应原 MultiplexPlayer::open() 的前端逻辑（onceToken 失败清理仍由调用方处理）
class FFmpegSource : public IStreamSource
{
public:
	explicit FFmpegSource(MultiplexPlayer* player) : player_(player) {}
	~FFmpegSource() override;

	bool prepare(const OpenContext& ctx) override;

	bool ptsIsMilliseconds() const override { return false; }  // 按 time_base 换算
	bool liveMode()        const override { return false; }     // FFmpeg 默认非 Live
	bool withOverlay()     const override { return true;  }
	HWND hwnd()            const override { return hwnd_; }

private:
	MultiplexPlayer* player_ = nullptr;
	HWND             hwnd_  = nullptr;
};

} // namespace muxplay
