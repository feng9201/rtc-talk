#pragma once
#include <atomic>
#include <mutex>
#include <string>
#include <cstdint>

struct AVFrame;
struct AVFormatContext;
struct AVCodecContext;
struct AVStream;
struct SwsContext;

namespace muxplay {

// ── 录制管理器：将解码后的视频帧编码为 H.264 写入 MP4（仅视频，不含音频）──
// 内部自带互斥锁，start/stop 与渲染线程的 onDecodedFrame 可并发调用。
class RecordingManager
{
public:
	RecordingManager() = default;
	~RecordingManager();  // 若仍在录制则自动 flush 并关闭文件

	// 开始录制。已存在文件将被覆盖。重复调用前需先 stop()。
	bool start(const std::string& filePath, int width, int height);
	// 停止录制并写入文件尾部。未在录制时返回 false。
	bool stop();
	bool isRecording() const { return recording_.load(std::memory_order_acquire); }

	// 由渲染线程 preRender 回调调用：录制中则编码写入一帧（内部持锁）
	void onDecodedFrame(AVFrame* frame);

private:
	bool openLocked(const std::string& path, int width, int height);
	bool closeLocked();
	bool writeFrameLocked(AVFrame* srcFrame);

	std::atomic<bool>   recording_{false};
	std::mutex          mutex_;
	std::string         path_;
	int64_t             pts_ = 0;

	AVFormatContext*    fmtCtx_ = nullptr;
	AVCodecContext*     encCtx_ = nullptr;
	AVStream*           stream_ = nullptr;
	AVFrame*            tmpFrame_ = nullptr;   // GPU→CPU 中转帧
	AVFrame*            encFrame_ = nullptr;   // 编码用帧（YUV420P）
	SwsContext*         swsCtx_ = nullptr;     // 像素格式转换
	int                 swsSrcWidth_ = 0;      // 当前 sws 的源宽度（用于检测重建）
	int                 swsSrcHeight_ = 0;     // 当前 sws 的源高度
	int                 swsSrcFormat_ = -1;    // 当前 sws 的源像素格式
};

} // namespace muxplay
