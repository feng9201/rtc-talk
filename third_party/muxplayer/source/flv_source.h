#pragma once
#include "istream_source.h"

namespace muxplay {

class MultiplexPlayer;

// ── HTTP-FLV 拉流源：cpr + flvparser 流式解析 ──
// 对应原 MultiplexPlayer::openFlv() 的前端逻辑（含 flvSetupThread_ 后台装配 + 老管线清理）
class FlvSource : public IStreamSource
{
public:
	explicit FlvSource(MultiplexPlayer* player) : player_(player) {}
	~FlvSource() override;

	bool prepare(const OpenContext& ctx) override;

	bool ptsIsMilliseconds() const override { return true;  }  // FLV PTS 已是毫秒
	bool liveMode()        const override { return liveMode_; }
	bool withOverlay()     const override { return true;   }
	HWND hwnd()            const override { return hwnd_;   }

private:
	MultiplexPlayer* player_   = nullptr;
	HWND             hwnd_     = nullptr;
	bool             liveMode_ = false;
};

} // namespace muxplay
