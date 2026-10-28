#pragma once
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <string>

struct AVFrame;

namespace muxplay {

// ── 截图管理器：按需捕获当前解码帧并保存为 JPEG ──
// 工作流程：
//   1. 调用方线程 save()：置 requested_，等待渲染线程捕获（最多 3s）
//   2. 渲染线程 onDecodedFrame()：发现请求后捕获当前帧并唤醒等待方
//   3. 调用方线程将捕获帧编码为 JPEG 写盘
class SnapshotManager
{
public:
	SnapshotManager() = default;
	~SnapshotManager();

	// 由渲染线程 preRender 回调调用：若存在挂起请求则捕获当前帧
	void onDecodedFrame(AVFrame* frame);

	// 请求截图并等待捕获完成，随后编码为 JPEG 保存。成功返回 true。
	bool save(const std::string& filePath);

	// 中止：丢弃请求并唤醒所有等待中的 save()（用于停止/销毁路径）
	void abort();

private:
	static bool encodeToJpeg(const AVFrame* srcFrame, const std::string& filePath);

	std::atomic<bool>       requested_{false};
	std::mutex              mutex_;
	std::condition_variable cv_;
	AVFrame*                frame_ = nullptr;   // 捕获到的帧
	bool                    ready_ = false;     // 捕获完成/被中止
	bool                    aborted_ = false;
};

} // namespace muxplay
