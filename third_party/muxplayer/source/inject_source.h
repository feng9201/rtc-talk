#pragma once
#include "istream_source.h"

namespace muxplay {

class MultiplexPlayer;

// ── 外部流注入源：通过 eventpp 回调接收编码后的 H.264/H.265 包 ──
// 结构特殊：prepare() 仅完成 codecContext 创建 + 回调注册即返回 true，
// 真正的解码器初始化 + 渲染管线装配在首个关键帧到达时懒触发。
class InjectSource : public IStreamSource
{
public:
	explicit InjectSource(MultiplexPlayer* player) : player_(player) {}
	~InjectSource() override;

	bool prepare(const OpenContext& ctx) override;

	bool ptsIsMilliseconds() const override { return true; }   // 注入流 PTS 已是毫秒
	bool liveMode()        const override { return true; }     // 无音频，帧到即渲
	bool withOverlay()     const override { return false; }    // 无窗口
	HWND hwnd()            const override { return nullptr; }

private:
	MultiplexPlayer* player_ = nullptr;
};

} // namespace muxplay
