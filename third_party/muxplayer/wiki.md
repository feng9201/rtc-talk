# MuxPlayer 修复记录

本文件记录 muxPlayer 模块在 HTTP-FLV 流媒体场景下排查和修复的稳定性问题。

---

## 1. FlvDemuxer 多线程竞态崩溃

### 现象
快速切换 FLV 流时崩溃，崩溃点分散在 `close()` 中的 `thread_->join()` 及之后。

### 根因
与 anglePlayer Wiki #1 相同：

| # | 竞态 | 描述 |
|---|------|------|
| 1 | Use-After-Free | `thread_` 被 `reset()` 后仍访问 |
| 2 | 并发 close() | 多个线程同时调用 `close()`，`unique_ptr<thread>` 无保护 |
| 3 | fail() TOCTOU | 检查对象存活后、使用前对象被销毁 |

### 修复

**文件：** `demuxing/flv_demuxing.h`、`demuxing/flv_demuxing.cpp`

- 添加 `startCloseMutex_` + `closed_`（atomic）保护 `start()`/`close()` 状态转换的原子性
- `close()` 分两步：锁内设置标志 + 取走 `thread_`，锁外 join（不持锁 join，避免阻塞 `start()`）
- `close()` 幂等：`closed_` 保证多次调用安全
- `start()` 不重置 `closed_`，避免覆盖已设置的停止信号

```cpp
void FlvDemuxer::close()
{
    std::unique_ptr<std::thread> threadToJoin;
    {
        std::lock_guard<std::mutex> lk(startCloseMutex_);
        if (closed_.load()) return;  // 幂等
        closed_.store(true);
        isStop_.store(true);
        threadToJoin = std::move(thread_);
    }
    if (threadToJoin && threadToJoin->joinable())
        threadToJoin->join();

    // 不调用 parser_->quit()（原因见第3节）

    {
        std::lock_guard<std::mutex> slock(seqMutex_);
        seqCv_.notify_all();
    }
}
```

---

## 2. FLVParser `hasVideo()`/`hasAudio()` 空指针崩溃

### 现象
cpr WriteCallback 中调用 `parser_->hasVideo()` 时崩溃，崩溃在 `_d->_header->hasVideo()`。

### 根因
与 anglePlayer Wiki #2 相同：
`FLVParser::parse()` 在数据不足（< 9 字节）时无法解析 FLV Header，`_d->_header` 仍为 `nullptr`。
但 `hasVideo()`/`hasAudio()` 无条件访问 `_d->_header`，导致空指针崩溃。

### 修复

**文件：** `demuxing/flv_demuxing.cpp`（WriteCallback）

- 检查 `parser_->parse()` 返回值，`false` 时跳过 `hasVideo()`/`hasAudio()`

```cpp
bool parsed = parser_->parse(reinterpret_cast<const uint8_t*>(data.data()), data.size());
if (!parsed) {
    return !isStop_.load();  // header 尚未解析完成，不能调用 hasVideo/hasAudio
}
auto hasVideo = parser_->hasVideo();
auto hasAudio = parser_->hasAudio();
```

---

## 3. FLVParser `quit()` 中 `_d` 空指针崩溃（Use-After-Free）及死锁风险

### 现象
`FLVParser::quit()` 崩溃在 `_d->_quit.store(true)`，此时 `_d` 为 null。

### 根因
与 anglePlayer Wiki #3 相同，两类问题：

1. **Use-After-Free**：跨线程 `close()` 通过裸指针调用 `parser_->quit()` 时，`FlvDemuxer` 已被其他线程 `reset()` 销毁，`parser_->_d` 已为空。

2. **死锁风险**：`ParseTag()` 中 `_packets` 队列满时在 `_quit` 循环中等 50ms 重试，但 `_quit` 要等 `close()` join 后才设置 → 循环依赖 → 死锁。

### 修复

**文件：** `demuxing/flv_demuxing.cpp`

- 从 `FlvDemuxer::close()` 和 `start()` 超时路径中移除 `parser_->quit()` 调用。
- 原因：`isStop_=true` 已让 WriteCallback 返回 false → cpr 退出 → demuxLoop 退出 → 线程已 join，parser 不再被并发访问。parser 的清理由 `~FlvDemuxer()` 中 `parser_` unique_ptr 自动完成。

---

## 4. demuxLoop HTTP 连接超时

### 问题
与 anglePlayer Wiki #6 相同：
- HTTP 无连接超时时，`cpr::Get` 可能长时间阻塞，导致 `close()` 中 join 等待很久，UI 卡死

### 修复

**文件：** `demuxing/flv_demuxing.h`、`demuxing/flv_demuxing.cpp`

- `demuxLoop` 签名增加 `int connectTimeoutMs` 参数
- `start()` 将 `timeoutMs` 传递给 `demuxLoop` 作为连接超时
- `cpr::Get` 添加 `cpr::ConnectTimeout{connectTimeoutMs}` 控制 TCP/SSL 连接超时
- `start()` 在 `startCloseMutex_` 保护下检查 `closed_`，已关闭则立即返回

---

## 5. 快速切换 FLV 流 cpr WriteCallback 崩溃（flvDemuxer_ 未 close 就 reset）

### 现象
快速切换 FLV 流（拉流→停流→拉流）时崩溃，崩溃点在 cpr WriteCallback 的 lambda 中（flv_demuxing.cpp），
即 WriteCallback 访问 `this`（FlvDemuxer*）时对象已被销毁。

### 根因
与 anglePlayer Wiki #8 相同，但 muxPlayer 场景更复杂：

`destryMux()` 中虽有 `flvDemuxer_->close()` + `reset()`，但 `openFlv()` 使用 `std::thread(...).detach()` 而非可 join 线程，
无法在下次 `openFlv` 时等待旧线程退出。旧 detach 线程可能在对象销毁后继续访问 `mux_private_`。

### 修复

**文件：** `multiplex_player_private.h`

- 添加 `std::unique_ptr<std::thread> flvSetupThread_` 成员，替代 `detach()` 线程

**文件：** `multiplex_player.cpp`（`openFlv()`）

- 将 `std::thread(...).detach()` 改为 `mux_private_->flvSetupThread_ = std::make_unique<std::thread>(...)`
- 在创建新线程前，先 close + join 旧 flvDemuxer_ 和旧 flvSetupThread_
- join 后再次检查 `flvDemuxer_` 并先调用 `close()` 再 `reset()`（close() 幂等，多次调用安全）

```cpp
// openFlv() 开头（UI 线程）：
if (mux_private_->flvDemuxer_) {
    mux_private_->flvDemuxer_->close();
}
if (mux_private_->flvSetupThread_ && mux_private_->flvSetupThread_->joinable()) {
    mux_private_->flvSetupThread_->join();
    mux_private_->flvSetupThread_.reset();
}
// join 后 flvDemuxer_ 可能已被 flvSetupThread_ 设置，必须先 close 再 reset
if (mux_private_->flvDemuxer_) {
    mux_private_->flvDemuxer_->close();
    mux_private_->flvDemuxer_.reset();
    mux_private_->isFlvStream_ = false;
}
```

---

## 6. `close()` 条件早返回跳过 FLV 清理

### 现象
`close()` 在 `player_status_` 不是 `GET_STREAM`/`FIRST_FRAME`/`PLAYING` 时提前 return，
跳过了后面的 `flvDemuxer_->close()` + `reset()` 和 `isFlvStream_ = false`。
当 `openFlv()` 正在后台线程执行（状态为 `OPENING`）时被 `close()` 中断，FLV 资源不会被释放。

### 修复

**文件：** `multiplex_player.cpp`（`close()`）

- 将 FLV 清理代码移到早返回判断之前
- FLV 清理包含：close flvDemuxer → join flvSetupThread_ → 再次 close+reset flvDemuxer_（同 anglePlayer 模式）
- 移除原位于早返回之后的 FLV 清理代码（避免重复）

```cpp
void MultiplexPlayer::close()
{
    mux_private_->is_stop_ = true;
    // ... join openThread_, notify CVs ...

    // FLV 清理（必须在早返回之前执行）
    if (mux_private_->flvDemuxer_) {
        mux_private_->flvDemuxer_->close();
    }
    if (mux_private_->flvSetupThread_ && mux_private_->flvSetupThread_->joinable()) {
        mux_private_->flvSetupThread_->join();
        mux_private_->flvSetupThread_.reset();
    }
    if (mux_private_->flvDemuxer_) {
        mux_private_->flvDemuxer_->close();
        mux_private_->flvDemuxer_.reset();
    }
    mux_private_->isFlvStream_ = false;

    if (player_status_ != GET_STREAM && ...) {
        return;  // FLV 已清理，安全返回
    }
    // ... 其余清理 ...
}
```

---

## 7. `fail()` lambda 不清理 flvDemuxer_

### 现象
`openFlv()` 中的 `fail()` lambda 只设置 `isFlvStream_ = false` 和回调通知，
不清理 `flvDemuxer_`。启动失败后 `flvDemuxer_` 残留，demuxLoop 线程继续运行，
下次 `openFlv()` 或 `close()` 时可能产生竞态。

### 修复

**文件：** `multiplex_player.cpp`（`openFlv()` 内 `fail()` lambda）

- `fail()` 中添加 `flvDemuxer_->close()` + `reset()` 清理

```cpp
auto fail = [this](E_PLAYER_STATUS st, const char* msg) {
    player_status_ = st;
    mux_private_->isFlvStream_ = false;
    if (mux_private_->flvDemuxer_) {
        mux_private_->flvDemuxer_->close();
        mux_private_->flvDemuxer_.reset();
    }
    if (mux_private_->statusCallback_) {
        mux_private_->statusCallback_(st, mux_private_->user_data_, msg);
    }
};
```

---

## 8. 硬解 + D3D 渲染窗口拖动/调整大小时闪烁

### 现象
硬解 + D3D 渲染模式下，拖动窗口标题栏或调整窗口边缘时，视频区域出现白底/黑底闪烁，画面无法稳定显示。

### 根因

两个独立原因叠加导致闪烁：

| # | 根因 | 描述 |
|---|------|------|
| 1 | Qt backing store GDI blit 覆盖 D3D | `play_widget` 是普通 QWidget，Qt 为其创建 `QWidgetBackingStore`。即使通过 `eventFilter` 吞掉 `QEvent::Paint`，平台插件仍通过 GDI `BitBlt` 将 backing store（默认背景色）刷到 HWND，覆盖 SwapChain 内容。**仅 `paintEngine() { return nullptr; }` 才能彻底禁用 backing store。** |
| 2 | ResizeBuffers 空帧间隙 | `ResizeBuffers()` 使用 `DXGI_SWAP_EFFECT_DISCARD`，重建后 back buffer 内容为空，front buffer 也失效。DWM 合成空帧直到下一次 `render() + present()`，至少 1 帧空白。 |

### 无效方案（v1）

v1 使用 `QAbstractNativeEventFilter` + play_widget 属性设置（`WA_PaintOnScreen` / `WA_OpaquePaintEvent` / `WA_NoSystemBackground`）+ `eventFilter` 吞 Paint 事件。失败原因：eventFilter 只能拦截 `paintEvent()`，无法阻止 Qt 平台插件通过 GDI 将 backing store blit 到 HWND。

### 修复方案（v2）

参考 zeroD3DPlayer 的 `RenderWidget`，创建自定义 D3DRenderWidget 替代 NativeEventFilter。

**修复点 1：D3DRenderWidget — `paintEngine()` 返回 nullptr**

**文件：** `muxDemo/D3DRenderWidget.h`、`muxDemo/D3DRenderWidget.cpp`

- `paintEngine() { return nullptr; }` 彻底禁用 Qt backing store，阻止 GDI blit
- `paintEvent()` 空实现，D3D 由独立 renderThread 控制
- `nativeEvent()` 拦截 `WM_ERASEBKGND`（阻止 Windows 擦白底）+ `WM_SIZE`（立即调用 ResizeToRect）
- 构造函数设置 `WA_NativeWindow` / `WA_PaintOnScreen` / `WA_OpaquePaintEvent` / `WA_NoSystemBackground`

```cpp
class D3DRenderWidget : public QWidget {
    Q_OBJECT
public:
    explicit D3DRenderWidget(QWidget* parent = nullptr);

    // 关键：禁用 Qt backing store，防止 GDI blit 覆盖 D3D 内容
    QPaintEngine* paintEngine() const override { return nullptr; }

protected:
    void paintEvent(QPaintEvent*) override {}  // 空实现
    bool nativeEvent(const QByteArray&, void* message, long* result) override;
};
```

D3D 路径下作为 `play_widget` 子控件创建，使用其 HWND 绑定 SwapChain：

```cpp
// muxPlayerUI.cpp — on_btn_play_clicked() 硬解+D3D 分支
if (!d3dWidget_) {
    d3dWidget_ = new D3DRenderWidget(ui.play_widget);
    d3dWidget_->setGeometry(ui.play_widget->rect());
    d3dWidget_->show();
    d3dWidget_->raise();
}
d3dWidget_->setResizeCallback([this](int w, int h) {
    if (player_) player_->resizeRender(w, h);
});
// 使用 D3DRenderWidget 的 HWND 绑定 SwapChain
HWND hwnd = d3dWidget_ ? (HWND)d3dWidget_->winId()
                        : (HWND)ui.play_widget->winId();
```

**修复点 2：ResizeToRect 后立即 present — 消除空帧间隙**

**文件：** `render/dx_render.cpp`

- `ResizeToRect()` 末尾添加 `ClearRenderTargetView` + `Present(0, 0)`
- `Present(0, 0)` 不等 VSync 立即提交，确保 DWM 尽快拿到有效内容
- 在 `d3dMutex_` 锁内执行，与 render 线程互斥安全

```cpp
void DxRender::ResizeToRect(uint32_t w, uint32_t h)
{
    // ... ResizeBuffers + 重建 RTV + 更新视口 ...

    // 立即清屏并 present，避免 DWM 合成空帧闪烁
    float black[4] = { 0, 0, 0, 1 };
    d3dImmediateContext_->ClearRenderTargetView(d3dRenderTargetView_.Get(), black);
    d3dSwapChain_->Present(0, 0);  // 立即提交，不等 VSync
}
```

---

## 10. H.264 yuvj420p 流 D3D11VA 硬件解码失败

### 现象
部分 H.264 FLV 流（profile=Baseline, `yuvj420p` full-range 色彩范围）在 D3D11VA 硬件解码时失败：
```
[h264] Format d3d11 requires hwaccel h264_d3d11va2 initialisation.
[h264] Failed setup for format d3d11: hwaccel initialisation returned error.
[h264] Format d3d11 not usable, retrying get_format() without it.
[h264] Format yuvj420p chosen by get_format().
```
系统 FFmpeg 7.1（`ffmpeg -hwaccel d3d11va`）同样降级到软件解码，证明这不是本项目代码问题，而是 D3D11VA 驱动限制。

### 根因
该流的 SPS 中 `vui_parameters_present_flag=1` 且 `video_full_range_flag=1`（full range JPEG YUV420P），D3D11VA 的 H.264 解码器在某些 GPU 驱动上不支持 `yuvj420p` 输出格式，`hwaccel->init` 返回错误。

**流参数：**
```
Video: h264 (Baseline), yuvj420p(pc, bt709, progressive, left), 1920x1080, 25 fps
SPS: profile_idc=66(Baseline) level_idc=41 max_ref_frames=1
extradata: 01 42 00 2A FF E1 00 19 67 42 00 29 ... (AVCDecoderConfigurationRecord)
```

### 修复
D3D11VA 降级到软件解码是正确行为（与 FFmpeg/VLC 一致）。关键在于确保软解降级后渲染管线不崩溃、能正常出图。

---

## 11. D3D11VA 降级软解后 GL 互操作渲染崩溃

### 现象
D3D11VA 降级到软件解码后，帧格式变为 `AV_PIX_FMT_YUVJ420P`（CPU 格式），但 GL 互操作渲染路径仍将 `frame->data[0]`（CPU 指针）当作 `ID3D11Texture2D*` 传给 `CopySubresourceRegion`，导致访问无效 COM 对象崩溃。

### 根因
`D3D11FrameRenderer::consume()` 未区分帧格式：
- `AV_PIX_FMT_D3D11`（硬解）：`frame->data[0]` = `ID3D11Texture2D*`
- `AV_PIX_FMT_YUVJ420P`（软解降级）：`frame->data[0]` = `uint8_t*` Y 平面指针

### 修复
**文件：** `render/d3d11_renderer.cpp`

```cpp
if (useGLInterop_ && nv12Share_) {
    if (frame->format == AV_PIX_FMT_D3D11) {
        // 硬解：解码器纹理 → NV12 拷贝
        nv12Share_->render(d3dContext_, decoderTex, ySubresource);
    } else {
        // 软解降级：CPU 帧 → 上传 NV12 纹理
        nv12Share_->renderFromCPU(d3dContext_, frame);
    }
}
```

**文件：** `render/nv12_share.h` / `render/nv12_share.cpp`

新增 `renderFromCPU()` 方法，将 CPU 帧（`YUV420P`/`YUVJ420P`/`NV12`）上传到 D3D11 NV12 纹理，复用 `renderNV12ToRGBA()` 渲染管线。

---

## 12. NV12 planar 格式 CPU 上传：UpdateSubresource / Map 限制

### 问题 1：UpdateSubresource 对 NV12 UV 子资源使用 D3D11_BOX 触发断点
`UpdateSubresource` 对 NV12 planar 格式的 subresource 1（UV 平面）使用 `D3D11_BOX` 参数会触发 D3D11 调试层断点。

### 问题 2：Map subresource 1 返回 E_INVALIDARG (0x80070057)
D3D11 NV12 staging 纹理**只允许 Map subresource 0**。subresource 0 的 `mapped.pData` 包含整个 Y + UV 数据：
- Y 平面：`base` ~ `base + RowPitch * height`
- UV 平面：`base + RowPitch * height` ~ `base + RowPitch * height * 3/2`

### 修复
使用 `Map` subresource 0 一次写入 Y 和 UV 两个平面，`Unmap` 后 `CopySubresourceRegion` 拷到 GPU 纹理：

```cpp
// Map staging 纹理（subresource 0 包含 Y + UV）
D3D11_MAPPED_SUBRESOURCE mapped = {};
ctx->Map(d3dNV12Staging_, 0, D3D11_MAP_WRITE, 0, &mapped);

uint8_t* base = (uint8_t*)mapped.pData;
int yPitch = mapped.RowPitch;

// Y 平面
for (int row = 0; row < h; row++)
    memcpy(base + row * yPitch, ySrc + row * linesize, w);

// UV 平面（紧接 Y 之后）
uint8_t* uvBase = base + yPitch * h;
// YUV420P → NV12 交错 UV
for (int row = 0; row < uvH; row++)
    for (int col = 0; col < uvW; col++) {
        uvBase[col * 2]     = uPlane[col];  // U
        uvBase[col * 2 + 1] = vPlane[col];  // V
    }

ctx->Unmap(d3dNV12Staging_, 0);
ctx->CopySubresourceRegion(d3dNV12_, 0, 0, 0, 0, d3dNV12Staging_, 0, nullptr);
```

### 新增资源
- `d3dNV12Staging_`：`D3D11_USAGE_STAGING` + `D3D11_CPU_ACCESS_WRITE` 的 NV12 纹理，在 `resize()` 中创建

---

## 13. FLV 拉流 SPS 预解析获取分辨率

### 现象
FLV 拉流模式（无 `AVFormatContext`）下，`avcodec_open2` 设置 `hw_device_ctx` 后无法从 extradata 解析 SPS，导致 `width=0 height=0 profile=-99`，D3D11VA 无法创建硬件表面。

### 修复
在 `avcodec_open2` 前手动解析 extradata 中的 SPS，预设 `width/height/profile/level/sw_pix_fmt`：

**文件：** `decoder/hw_decoder.cpp`

```cpp
// H.264 AVCDecoderConfigurationRecord: 偏移 5=numSPS, 6-7=SPS长度, 8+=SPS NALU
int spsLen = (ed[6] << 8) | ed[7];
const uint8_t* spsNalu = ed + 8;
int w, h, fps, maxRef;
if (h264_decode_sps(spsNalu, spsLen, w, h, fps, maxRef)) {
    ctx->width = w;
    ctx->height = h;
    ctx->profile = spsNalu[1];  // profile_idc
    ctx->level = spsNalu[3];    // level_idc
    ctx->sw_pix_fmt = AV_PIX_FMT_NV12;  // D3D11VA 输出格式
}
```

**SPS 解析器：** `demuxing/h264_spsDecoder.h`（H.264）、`demuxing/hevc_spsDecoder.h`（HEVC）

**注意：** `sw_pix_fmt` 必须设为 `AV_PIX_FMT_NV12`(23)，不能设为 `AV_PIX_FMT_YUV420P`(0)——D3D11VA 解码输出的是 NV12 格式纹理，`av_hwframe_ctx_init` 创建表面池时要求 `sw_format` 与 D3D11VA 输出格式匹配。

---

## 14. hw_frames_ctx 手动创建

### 现象
`avcodec_open2` 后 `hw_frames_ctx` 为 null（未自动创建），D3D11VA `hwaccel->init` 在首次 `avcodec_send_packet` 时找不到表面池，返回 `Failed setup for format d3d11`。

### 修复
在 `avcodec_open2` 后手动创建 `hw_frames_ctx`（D3D11VA 表面池）：

```cpp
AVBufferRef* frames_ref = av_hwframe_ctx_alloc(hw_device_ctx_);
AVHWFramesContext* frames_ctx = (AVHWFramesContext*)frames_ref->data;
frames_ctx->format = (AVPixelFormat)pix_format_;  // AV_PIX_FMT_D3D11VA_VLD
frames_ctx->sw_format = AV_PIX_FMT_NV12;
frames_ctx->width = ctx->width;
frames_ctx->height = ctx->height;
frames_ctx->initial_pool_size = 20;
AVD3D11VAFramesContext* d3d11_frames = (AVD3D11VAFramesContext*)frames_ctx->hwctx;
d3d11_frames->BindFlags = D3D11_BIND_DECODER;
av_hwframe_ctx_init(frames_ref);
ctx->hw_frames_ctx = frames_ref;
```

---

## 9. RenderScheduler 网络抖动卡顿（无音频场景 `scheduledWallTime_` 重置丢失节拍）

### 现象
无音频流 + 非 live_mode 模式下（如纯视频 HTTP-FLV 流），网络抖动后画面卡顿，
抖动恢复后视频节奏无法追平源 PTS，最终与"应有进度"产生永久偏移。

### 根因

`RenderScheduler::waitForFrame()` 在队列空等返回时执行：
```cpp
clock_->lastDequeueWallGapMs = 0;
scheduledWallTime_ = std::chrono::steady_clock::now();  // ← 病灶
```

`scheduledWallTime_` 是渲染节拍累加基准（见 [render_scheduler.cpp:159-160](muxPlayer/pipeline/render_scheduler.cpp#L159-L160)）：
```cpp
scheduledWallTime_ = scheduledWallTime_ + d.intervalMs;
sleepUntil(scheduledWallTime_);
```

将其重置为 `now` 的副作用：
1. **节拍基准被向后拉**：空等 N 毫秒后基准跳到当前时刻，等价于"承认这段时间什么都没播放"
2. **时间静默吞掉**：抖动期间本应消费的 N 帧 PTS 进度丢失，后续每帧仍按 `frameIntervalMs` 节拍 sleep
3. **无补偿可用**：
   - 无音频时 `videoAudioDeltaMs()` 恒为 0 → [sync_strategy.cpp](muxPlayer/pipeline/sync_strategy.cpp) 的丢帧/加速/减速分支 1-5 全失效
   - `lastDequeueWallGapMs` 已被置 0 → 分支 6（GOP 边界补偿）也无法感知空等
   - 队列深度 `pendingFrameCount` 仅用于诊断，未参与决策
4. **结果**：抖动恢复后渲染节拍从抖动时刻重新计时，丢失时间不可追回 → 画面相对源 PTS 永久落后

### 备选方案对比

| # | 方案 | 收益 | 成本 | 副作用 | 总分 |
|---|------|------|------|--------|------|
| 1 | `waitForFrame` 不抹零 wallGap，让 GOP 补偿分支感知空等 | ★★☆ | ★★★ | ★★☆ | 7.5 |
| 2 | 最小队列水位 jitter buffer | ★★★ | ★★☆ | ★（需配 1） | 6.0 |
| 3 | `waitForFrame` 不重置 `scheduledWallTime_` | ★★★★ | ★★★ | ★★★ | **10.0** |
| 4 | PTS 落后补偿（多帧压缩间隔追进度） | ★★★★ | ★ | ★★ | 8.0 |

### 选定方案：方案 3（根因修复）

**理由**：
- 直击病灶：当前卡顿本质 = 节拍基准被空等向后拉
- 改动 1 行（删除赋值），回归风险极低
- 与现有 `sleepUntil` 自然协作：target < now 时立即返回 = 自动连续渲染积压帧
- 无新状态机、无 PTS 时基计算，契合"纯数据 MediaClock + 纯函数 SyncStrategy"架构
- 副作用可控：最坏情况是抖动后短时帧率略快（追进度），由下一轮 `frameIntervalMs` 自适应快速收敛

### 修复

**文件：** `pipeline/render_scheduler.cpp`（`waitForFrame`）

- 删除 `scheduledWallTime_ = std::chrono::steady_clock::now();` 赋值
- 保留 `clock_->lastDequeueWallGapMs = 0;`（防止空等被 `decide()` 误判为 GOP 边界、与方案 1 解耦）

```cpp
bool RenderScheduler::waitForFrame(...)
{
    std::unique_lock<std::mutex> lock(*cvMutex_);
    frameCv_->wait_for(lock, std::chrono::milliseconds(10), [&] {
        return frameQueue.size_approx() > 0 || stopFlag;
    });
    // 更新壁钟追踪，防止空等被误计为 wallGap
    clock_->lastDequeueWallGapMs = 0;
    // 方案3：不重置 scheduledWallTime_。
    // 原实现 scheduledWallTime_ = now 会把空等期间的时间静默吞掉，
    // 导致抖动恢复后视频永久落后于源 PTS、节奏不可追回。
    // 保留旧基准后，下一轮 sleepUntil(scheduledWallTime_ + interval) 会
    // 因 target < now 立即返回，自动连续渲染积压帧追平进度，
    // 节拍基准只向前推进、不向后拉。
    return !stopFlag;
}
```

### 自洽性验证

抖动恢复后的循环行为：

1. 队列批量到达 N 帧
2. 首帧：`d = strategy_->decide(*clock_)`，因 `delta=0` 且 `wallGap=0` → NORMAL，`interval=frameIntervalMs`
3. 执行：`scheduledWallTime_ += interval` → 仍远小于 now → `sleepUntil` 立即返回 → 渲染下一帧
4. 重复 2-3，直到 `scheduledWallTime_` 追上 now
5. 此后每帧正常 sleep `interval`，稳态恢复

期间**无任何额外参数**，完全靠"节拍基准单调向前 + sleepUntil 自动跳过过期时刻"实现追赶。

### 局限性

- 抖动跨度极大（秒级以上）时，追赶期间帧率会瞬时拉满（受 `consume` 耗时限制），观感为短暂快放
- 无音频参照下仍无法精确对齐源 PTS，仅能保证"按节拍消费到积压的所有帧"
- 若需精确 PTS 对齐，应升级至方案 4（PTS 落后补偿），但当前场景不推荐（过度工程）

---

## 修改文件清单

| 文件 | 修改内容 |
|------|---------|
| `demuxing/flv_demuxing.h` | 添加 `startCloseMutex_`/`closed_`，`demuxLoop` 增加 `connectTimeoutMs` 参数，线程安全设计文档 |
| `demuxing/flv_demuxing.cpp` | close() 两步式重构、start() 加锁、WriteCallback 空指针保护、ConnectTimeout、移除 parser_->quit() |
| `multiplex_player_private.h` | 添加 `flvSetupThread_` 成员 |
| `multiplex_player.cpp` | openFlv 替换 detach 为 flvSetupThread_ + 开头清理旧 FLV、close() FLV 清理前置、fail() 清理 flvDemuxer_ |
| `muxDemo/D3DRenderWidget.h` | D3D 渲染专用 Widget，`paintEngine() { return nullptr; }` 禁用 Qt backing store |
| `muxDemo/D3DRenderWidget.cpp` | nativeEvent 拦截 WM_ERASEBKGND + WM_SIZE，构造函数设置防闪烁属性 |
| `muxDemo/muxPlayerUI.h` | 新增 `d3dWidget_` 成员，移除废弃的 `filter_` 和 `eventFilter` |
| `muxDemo/muxPlayerUI.cpp` | D3D 路径创建 D3DRenderWidget 并绑定 HWND，清理旧属性/eventFilter/NativeEventFilter |
| `render/dx_render.h` | 新增 `ResizeToRect()` 声明 |
| `render/dx_render.cpp` | 新增 `ResizeToRect()` 实现，末尾 `ClearRenderTargetView` + `Present(0,0)` 消除空帧 |
| `pipeline/render_scheduler.cpp` | `waitForFrame` 删除 `scheduledWallTime_ = now` 重置，保留节拍基准以修复无音频场景网络抖动卡顿 |

