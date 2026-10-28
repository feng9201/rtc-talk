# MuxPlayer

基于 FFmpeg + D3D11VA / OpenGL 的多路径视频播放库，支持硬件零拷贝渲染、软件 YUV→RGB 渲染、HTTP-FLV 拉流、D3D Overlay 叠加、画中画（PIP）及音频解码播放。

## 功能特性

| 特性 | 说明 |
|------|------|
| **双解码路径** | ① D3D11VA 硬件解码（零拷贝 GPU→GPU） ② FFmpeg 软件解码（CPU YUV） |
| **四种渲染模式** | ① 硬解+D3D SwapChain 直渲 ② 硬解+GL 互报渲染 ③ 软解+D3D SwapChain ④ 软解+GL YUV Shader |
| **双拉流协议** | ① RTSP / 文件（FFmpeg avformat） ② HTTP-FLV（cpr + flvparser 流式解析） |
| **音频播放** | mtAudio 统一解码库，支持 AAC / Opus / G.711A / G.711U，miniaudio 播放输出 |
| **AV / 无音频调度** | 三种时钟主控：① 有音频 → 音频 PTS 驱动 ② 无音频 → **PTS 壁钟直驱** ③ 无音频低延迟 → **Live 帧到即渲**；`SyncStrategy` 可配置容差 |
| **D3D Overlay** | 文字/图片叠加层（GDI 渲染到 D3D11 纹理），支持动态更新、颜色/位置/透明度控制 |
| **画中画 (PIP)** | D3D SwapChain 模式下同一帧 NV12 纹理二次绘制，支持拖拽移动、DPI 适配 |
| **高 DPI 适配** | `GetDpiForWindow` 自动检测缩放比，Overlay/PIP 坐标逻辑→物理像素自动转换 |
| **接口化管线** | `IDecoder` → `IFrameConsumer` 两层抽象，解码/渲染解耦 |
| **防闪烁 Resize** | `resizeRender()` 在 WM_SIZE 时即时 ResizeBuffers，避免 Qt 布局延迟导致的黑帧 |
| **线程安全** | D3D11 ImmediateContext 三线程（decoder/render/GL）通过 `d3dMutex_` 串行化 |
| **GPU 选取** | 支持枚举显卡并指定解码+渲染使用的 GPU（`setPreferGPU`） |
| **NVIDIA Optimus** | 内置 `NvOptimusEnablement` / `AmdPowerXpressRequestHighPerformance` 强制独显 |
| **Qt 集成** | `GLPlayWidget`（QOpenGLWidget）+ `D3DRenderWidget`（纯 D3D11 HWND）双渲染路径 |
| **截图保存** | `saveSnapshot(path)` 将当前帧保存为 JPEG，硬解自动 GPU→CPU 传输 |
| **录制保存** | `startRecording(path)` / `stopRecording()` 录制为 H.264 MP4（仅视频） |
| **SEI 回调** | `setSeiCallback` 提取 H.264/H.265 SEI user_data 并回传 |
| **状态回调** | 完整生命周期状态机（IDLE → OPENING → PLAYING → CLOSING） |

## 架构

### 整体管线

```
                ┌─────────────────────────────────────────────┐
                │              MultiplexPlayer                 │
                │  ┌──────────┐  ┌──────────┐  ┌──────────┐  │
                │  │ Demuxing │→│ Decoder  │→│ Consumer │  │
                │  │  Thread  │  │  Thread  │  │  Thread  │  │
                │  └──────────┘  └──────────┘  └──────────┘  │
                │        ↓             ↓              ↓        │
                │  vid_packetQ    decoded_frameQ    onFrame   │
                │                                  Ready()    │
                └──────────────────────┬──────────────────────┘
                                       │ callback
                ┌──────────────────────▼──────────────────────┐
                │              GLPlayWidget                    │
                │  ┌──────────────────────────────────────┐   │
                │  │        IDisplayConsumer               │   │
                │  │  ┌─────────────────┐ ┌────────────┐  │   │
                │  │  │WGLInteropDisplay│ │SoftGLDisplay│  │   │
                │  │  │ (WGL interop)   │ │ (YUV→RGB GL)│  │   │
                │  │  └────────┬────────┘ └─────┬──────┘  │   │
                │  │           ↓                 ↓         │   │
                │  │    D3D11 Shared      FramePump→CPU    │   │
                │  │    RGBA Texture      YUV Frame        │   │
                │  └──────────────────────────────────────┘   │
                │              QOpenGLWidget                   │
                └─────────────────────────────────────────────┘
```

### 四渲染模式对比

| | 硬解 + D3D 直渲 | 硬解 + GL 互报 | 软解 + D3D 直渲 | 软解 + GL Shader |
|---|---|---|---|---|
| **解码器** | `HardwareDecoder` (D3D11VA) | `HardwareDecoder` (D3D11VA) | `SoftwareDecoder` (FFmpeg CPU) | `SoftwareDecoder` (FFmpeg CPU) |
| **帧消费器** | `D3D11FrameRenderer` | `D3D11FrameRenderer` | `D3D11FrameRenderer` | `FramePump` |
| **渲染后端** | SwapChain Present | WGL_NV_DX_interop | SwapChain Present | GLPlayWidget YUV Shader |
| **输出格式** | D3D11 NV12→RGBA | D3D11 共享 RGBA → GL 纹理 | CPU YUV420P→D3D11 NV12→RGBA | CPU YUV420P → GL 纹理 |
| **帧传输** | 零拷贝 GPU→GPU | 零拷贝 GPU→GPU | CPU 内存→GPU 上传 | CPU 内存引用计数 |
| **Overlay/PIP** | ✅ 支持 | ❌ | ✅ 支持 | ❌ |
| **依赖** | NVIDIA GPU | NVIDIA GPU + WGL 扩展 | 任意 D3D11 设备 | 任意 OpenGL 3.3+ |
| **设置方法** | `Hardware` + `setGLInteropMode(false)` | `Hardware` + `setGLInteropMode(true)` | `Software` + `setGLInteropMode(false)` | `Software` + `setGLInteropMode(true)` |

### 硬件路径数据流（D3D SwapChain 直渲）

```
[RTSP/文件/FLV]
  → av_read_frame() / cpr+flvparser
    → enqueueVideoPacket()           ── Demux Thread
      → avcodec_send_packet()
        → D3D11VA GPU 解码
          → avcodec_receive_frame() ── Decoder Thread
            → CopySubresourceRegion (NV12 → NV12)
              → D3D11 PS: NV12 → RGBA
                → [PIP 二次绘制]     ── 同一帧 NV12 纹理在指定位置二次绘制
                → [Overlay 叠加]    ── GDI 文字/图片渲染到 D3D11 纹理
                → SwapChain Present()     ── Render Thread
```

### 硬件路径数据流（GL 互报渲染）

```
[RTSP/文件/FLV]
  → av_read_frame() / cpr+flvparser
    → enqueueVideoPacket()           ── Demux Thread
      → avcodec_send_packet()
        → D3D11VA GPU 解码
          → avcodec_receive_frame() ── Decoder Thread
            → CopySubresourceRegion (NV12 → NV12)
              → D3D11 PS: NV12 → RGBA (共享纹理)
                → wglDXLockObjectsNV
                  → GL texture() 直读 → 显示  ── Qt GL Thread
```

### 软件路径数据流（D3D SwapChain）

```
[RTSP/文件/FLV]
  → av_read_frame() / cpr+flvparser
    → enqueueVideoPacket()           ── Demux Thread
      → avcodec_send_packet()
        → FFmpeg CPU 解码 (YUV420P)
          → avcodec_receive_frame() ── Decoder Thread
            → UpdateSubresource (YUV420P → NV12) ── CPU 上传到 D3D11 纹理
              → D3D11 PS: NV12 → RGBA
                → [PIP 二次绘制]     ── 同一帧 NV12 纹理在指定位置二次绘制
                → [Overlay 叠加]    ── GDI 文字/图片渲染到 D3D11 纹理
                → SwapChain Present()     ── Render Thread
```

### 软件路径数据流（GL Shader）

```
[RTSP/文件/FLV]
  → av_read_frame() / cpr+flvparser
    → enqueueVideoPacket()           ── Demux Thread
      → avcodec_send_packet()
        → FFmpeg CPU 解码 (YUV420P)
          → avcodec_receive_frame() ── Decoder Thread
            → FramePump::consume()   ── Render Thread
              → GLYuvShader::uploadFrame (Y/U/V → GL纹理)
                → GLSL: YUV → RGB → 显示  ── Qt GL Thread
```

### 音频数据流

```
[FLV 音频包]  ── FlvDemuxer::audioCallback_
[FFmpeg 音频包] ── FFmpegDemuxing
  → enqueueAudioPacket()             ── Demux Thread
    → audioDecodeLoop()              ── Audio Decode Thread
      → mtAudio::AudioDecoder::decode()
        → audioFrameCallback_(frame) ── 外部播放 (miniaudio)
```

### 渲染调度：三种时钟主控

`RenderScheduler` + `MediaClock` + `SyncStrategy` 统一调度视频出帧。无音频时**不只**有 Live 模式，还有 **PTS 壁钟直驱** 模式：

| 模式 | `ClockMaster` | 触发条件 | 行为 | 典型场景 |
|------|---------------|----------|------|----------|
| **AV 同步** | `Audio` | 启用音频解码 | 以音频播放 PTS 为基准，视频追音频（加速/减速/丢帧） | 有音轨的 RTSP/FLV/文件 |
| **PTS 壁钟直驱** | `PTS` | 无音频且非 Live | 首帧锚定 `(anchorPts, anchorWallTime)`，目标壁钟 = 锚点壁钟 + (当前 PTS − 锚点 PTS)，`sleepUntil` 到点再渲；网络抖动靠早到等待、晚到追赶吸收 | 纯视频网络流（如无音轨 RTSP、`open` 且 `enableAudio=false`） |
| **Live** | `None`（`live_mode_`） | `setupRenderPipeline(..., liveMode=true)` | 帧到即渲，跳过 PTS 调度与 sleep，追求最低延迟 | 本地摄像头 / 外部注入流（`injectExternalStream`）；FLV 关闭音频时也会走 Live |

```
有音频 (enableAudio=true)
  └─ ClockMaster::Audio ── 音频时钟驱动视频

无音频
  ├─ liveMode=true  ── Live：帧到即渲（低延迟，不抗网络抖动）
  └─ liveMode=false ── PTS 壁钟直驱：按视频时间戳对齐壁钟（可吸收抖动）
```

装配逻辑（`setupRenderPipeline`）：

```cpp
if (liveMode) {
    renderScheduler_->setLiveMode(true);           // Live
} else if (!audioEnabled_) {
    renderScheduler_->setClockMaster(ClockMaster::PTS);  // PTS 壁钟直驱
}
// else 默认 ClockMaster::Audio
```

各流源默认：

| 流源 | `liveMode` | 无音频时实际调度 |
|------|------------|------------------|
| `FFmpegSource`（`open` / `openAsync`） | `false` | **PTS 壁钟直驱** |
| `FlvSource`（`openFlv`） | `!enableAudio` | 关音频 → **Live**；开音频 → AV 同步 |
| `InjectSource`（`injectExternalStream`） | `true` | **Live** |

> PTS 壁钟直驱公式：`targetWall = anchorWallTime + (framePtsMs - anchorPtsMs)`。首帧渲染时写入锚点；后续帧按相对 PTS 对齐壁钟，早到则等、晚到则尽快追。

### 接口抽象

```cpp
// 解码器接口
class IDecoder {
    virtual bool decode(DecodedFrame& out) = 0;
    virtual bool hasPendingPacket() const = 0;
    virtual void close() = 0;
};

// 帧消费器接口（运行在 render 线程）
class IFrameConsumer {
    virtual void consume(AVFrame* frame) = 0;
    virtual void setOnFrameReady(std::function<void()> cb) = 0;
};
```

## 目录结构

```
MuxPlayer/
├── muxPlayer/                      # 核心播放库 (muxPlayer.dll/lib)
│   ├── include/                   # 公共头文件
│   │   ├── multiplex_player.h     #   核心 API
│   │   ├── gl_play_widget.h       #   Qt GL 渲染 Widget
│   │   └── gl_interop.h           #   WGL_NV_DX_interop 封装
│   ├── pipeline/                  # 接口层 + AV 同步
│   │   ├── frame_types.h          #   FrameMemoryType / DecodedFrame
│   │   ├── idecoder.h             #   IDecoder 抽象
│   │   ├── iframe_consumer.h      #   IFrameConsumer 抽象
│   │   ├── idisplay_consumer.h    #   IDisplayConsumer 抽象
│   │   ├── media_clock.h          #   AV 同步时钟（音频 PTS 驱动）
│   │   ├── render_scheduler.h     #   渲染调度器（帧队列→consume→Present）
│   │   └── sync_strategy.h        #   AV 同步策略（音频时钟驱动）
│   ├── decoder/                   # 解码器实现
│   │   ├── hw_decoder.h/cpp       #   D3D11VA 硬件解码器
│   │   ├── sw_decoder.h/cpp       #   FFmpeg 软件解码器
│   │   └── ffmpeg_decoder.h/cpp   #   send/receive 解码循环
│   ├── render/                    # 渲染 / GL Widget / Overlay
│   │   ├── d3d11_renderer.h/cpp   #   D3D11 帧渲染器 (IFrameConsumer)
│   │   ├── frame_pump.h/cpp       #   软解帧中继 (IFrameConsumer)
│   │   ├── gl_play_widget.cpp     #   GLPlayWidget 实现
│   │   ├── gl_interop.cpp         #   WGL 互操作实现
│   │   ├── dx_render.h/cpp        #   SwapChain 渲染后端 (NV12→RGBA + PIP)
│   │   ├── nv12_share.h/cpp       #   NV12→RGBA 共享纹理 (GL 互报)
│   │   ├── overlay_manager.h/cpp  #   D3D Overlay 管理器 (文字/图片叠加)
│   │   ├── text_renderer.h/cpp    #   GDI 文字渲染到 D3D11 纹理
│   │   ├── image_renderer.h/cpp   #   图片渲染到 D3D11 纹理
│   │   └── shader_manager.h/cpp   #   HLSL 着色器编译/管理
│   ├── display/                   # 显示策略
│   │   ├── wgl_interop_display.h/cpp  # WGL 显示 (IDisplayConsumer)
│   │   ├── soft_gl_display.h/cpp      # 软解 GL 显示 (IDisplayConsumer)
│   │   └── gl_yuv_shader.h/cpp        # YUV→RGB GLSL 着色器
│   ├── demuxing/                  # 解封装
│   │   ├── ffmpeg_demuxing.h/cpp  #   RTSP/文件拉流
│   │   └── flv_demuxing.h/cpp     #   HTTP-FLV 拉流 (cpr+flvparser)
│   ├── multiplex_player.cpp       # 核心调度
│   └── multiplex_player_private.h # 私有状态

├── thirdPart/
│   ├── flvparser/                  # FLV 解封装库
│   ├── mtAudio/                    # 统一音频解码库 (mtAudio.lib)
│   │   ├── include/               #   公共 API (mt_audio_decoder.h)
│   │   └── src/                   #   AAC / Opus / G.711 解码器实现
│   └── mtAudioPlayer/             # 音频播放库 (miniaudio 封装)
├── muxPlayerPreview/              # Qt 预览 Demo (调用 anglePlayer)
├── muxDemo/                       # Qt 测试 Demo (调用 muxPlayer)
│   ├── D3DRenderWidget.h/cpp     #   D3D11 原生 HWND 渲染控件 (PIP 拖拽)
│   ├── muxPlayerUI.h/cpp         #   主窗口 UI (四种模式切换)
│   ├── main.cpp                  #   入口
│   └── system_monitor.h          #   CPU/GPU 使用率监控
├── zeroD3DPlayer/                 # 独立 D3D11 播放器库 (纯 D3D，不依赖 muxPlayer)
│   ├── include/                   #   D3D11Render.h / Player.h
│   ├── src/                       #   Player / Decoder / D3D11Device / 各 Renderer
│   └── CMakeLists.txt
├── zeroD3DDemo/                   # zeroD3DPlayer 的 Qt Demo 应用
│   ├── MainWindow.h/cpp           #   主窗口 (含 PIP 拖拽、Overlay 控制面板)
│   ├── RenderWidget.h/cpp         #   D3D11 HWND 渲染控件
│   └── XvidDecoder.h/cpp          #   Xvid 软解码器
├── anglePlayer/                   # ANGLE/ES 渲染播放器 (独立模块)
├── angleDemo/                     # anglePlayer 的 Qt Demo 应用
├── CMakeLists.txt                 # 顶层 CMake
├── vcpkg.json                     # 依赖声明
├── vcpkg-configuration.json       # vcpkg registry
├── build.bat                      # 一键构建脚本
└── WIKI.md                        # 技术文档
```

## 构建

### 环境要求

| 组件 | 版本 |
|------|------|
| Visual Studio | 2022 (MSVC) |
| CMake | ≥ 3.17 |
| Qt | 5.15.2 (Core/Gui/Widgets/OpenGL) |
| FFmpeg | 5.1.2 (avcodec/avformat/avutil) |
| vcpkg | 含 x64-windows triplet |

### 一键构建

```bat
build.bat
```

脚本自动执行：
1. CMake 配置 (Visual Studio 2022, x64, Debug)
2. 编译 `muxPlayer.lib` + `muxPlayerPreview.exe`
3. `windeployqt` 部署 Qt DLL
4. 复制运行时依赖 DLL (`vs-dll/*.dll`)

### 手动构建

```bat
cmake -B build -G "Visual Studio 17 2022" -T host=x64 ^
  -DCMAKE_BUILD_TYPE=Debug ^
  -DQT5_PATH=D:\Qt\5.15.2\msvc2019_64\lib\cmake ^
  -DCMAKE_TOOLCHAIN_FILE=D:/vcpkg/scripts/buildsystems/vcpkg.cmake ^
  -DVCPKG_TARGET_TRIPLET=x64-windows

cmake --build build --config Debug
```

### CMake 选项

| 选项 | 默认 | 说明 |
|------|------|------|
| `ENABLE_WGL_INTEROP` | ON | 启用 WGL_NV_DX_interop + QOpenGLWidget 支持 |
| `MUXPLAY_ENABLE_FLV` | ON | 启用 HTTP-FLV 拉流功能（依赖 cpr + OpenSSL） |
| `QT5_PATH` | — | Qt5 CMake 模块路径 |
| `MUXPLAY_BUILD_SHARED` | OFF | 构建为 DLL（默认静态库） |

### 运行时依赖

| DLL | 来源 |
|-----|------|
| `avcodec-59.dll`, `avformat-59.dll`, `avutil-57.dll` | FFmpeg (vcpkg) |
| `swresample-4.dll`, `swscale-6.dll` | FFmpeg (vcpkg) |
| `fdk-aac.dll`, `libopus.dll` | 音频解码 (vcpkg) |
| `libx264-164.dll` | x264 编码 (vcpkg) |
| `libcurl.dll`, `cpr.dll` | HTTP-FLV 拉流 (vcpkg) |
| `libcrypto-3-x64.dll`, `libssl-3-x64.dll` | HTTPS 加密 (OpenSSL) |
| `Qt5Core.dll`, `Qt5Gui.dll`, `Qt5Widgets.dll`, `Qt5OpenGL.dll` | Qt |

## 使用

### 1. 集成到项目

```cmake
# 你的 CMakeLists.txt
target_link_libraries(your_app
    PRIVATE muxPlayer mtAudio mtAudioPlayer
)
target_include_directories(your_app
    PRIVATE ${MUXPLAYER_DIR}/muxPlayer/include
    PRIVATE ${MUXPLAYER_DIR}/mtAudio/include
    PRIVATE ${MUXPLAYER_DIR}/mtAudioPlayer/include
)
```

### 2. 基本播放

```cpp
#include <multiplex_player.h>
#include <gl_play_widget.h>

// 创建播放器
auto player = new muxplay::MultiplexPlayer();

// ── 选择解码模式 ──
player->setDecodeMode(muxplay::DecodeMode::Hardware);  // 硬解（默认）
// player->setDecodeMode(muxplay::DecodeMode::Software); // 软解

// ── 选择渲染模式（硬解可选）──
player->setGLInteropMode(false);  // D3D SwapChain 直渲（硬解默认）
// player->setGLInteropMode(true); // GL 互报渲染（需创建 GLPlayWidget）

// ── 指定 GPU（可选，枚举由 tmcore 提供）──
#include <tmcore/GpuInfo.h>
auto gpus = tmcore::GpuInfoQuery::Enumerate();
player->setPreferGPU(static_cast<int>(gpus[0].index));

// ── GL 渲染时创建 GLPlayWidget ──
auto glWidget = new GLPlayWidget(parentWidget);
glWidget->setDecodeMode(player->decodeMode());
glWidget->setPlayer(player);
player->setGLInteropMode(true);
player->setFrameRenderedCallback([glWidget]() {
    if (glWidget) glWidget->requestNewFrame();
});

// ── 异步打开流（RTSP/文件）──
player->openAsync("rtsp://192.168.1.100:8554/stream",
                  (HWND)parentWidget->winId(),
                  5000, nullptr,
                  [](auto status, void*, const char* msg) {
                      qInfo() << "Status:" << (int)status << msg;
                  },
                  true);  // 启用音频

// ── HTTP-FLV 拉流 ──
player->openFlv("https://server/live/stream.flv",
                (HWND)parentWidget->winId(),
                5000, nullptr, statusCb, true);
```

### 3. 音频播放

```cpp
#include <mt_audio_player.h>

// ── 设置音频帧回调（解码后的 PCM 输出）──
player->setAudioFrameCallback([&audioPlayer](const mtAudio::AudioFrame& frame) {
    if (!audioPlayer && !frame.empty()) {
        audioPlayer = mtAudio::IAudioPlayer::create();
        audioPlayer->init(frame.sampleRate, frame.channels, frame.bitsPerSample);
        audioPlayer->play();
    }
    if (audioPlayer) audioPlayer->pushFrame(frame);
});

// ── AV 同步时钟回调（renderLoop 查询当前音频 PTS）──
player->setAudioClockCallback([&audioPlayer]() -> int64_t {
    return audioPlayer ? audioPlayer->getPlayingPts() : -1;
});
```

### 4. 状态回调

```cpp
// 状态枚举
enum class E_PLAYER_STATUS {
    PLAYER_STATUS_IDLE,
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
};

// 回调签名
using PlayerStatusCallback = std::function<void(E_PLAYER_STATUS, void* user_data, const char* msg)>;
```

### 5. 常用 API

```cpp
// 控制
player->close();           // 停止播放
player->pause();           // 暂停（当前为空实现）
player->replay();          // 重播（当前为空实现）

// 查询
auto info = player->getVideoInfo();
// info.width, info.height, info.bitrate, info.frameRate, info.codecName

bool running = player->isRunning();

// GL Widget 控制
glWidget->requestNewFrame();   // 触发刷新
glWidget->setKeepLastFrame(false);  // 停止后不保留最后一帧
glWidget->clearFrame();        // 清屏为黑色

// D3D 线程安全（WGL interop 路径使用）
player->lockD3DContext();
// ... WGL 操作 ...
player->unlockD3DContext();

// 软解帧访问（仅 Software 模式）
auto* pump = player->getFramePump();
if (pump) {
    AVFrame* frame = pump->acquireLatestFrame();
    // 处理 YUV 数据...
    av_frame_free(&frame);
}

// 截图（硬解/软解均支持）
bool ok = player->saveSnapshot("D:/snapshot.jpg");
// 硬解模式下内部自动做一次 GPU→CPU 传输（av_hwframe_transfer_data），耗时约数毫秒

// 录制（硬解/软解均支持，open 之后调用）
player->startRecording("D:/record.mp4");
// ... 播放一段时间 ...
player->stopRecording();
// MP4 文件写入完毕。注意：close() 前若仍在录制会自动停止并冲洗写入。
// 硬解模式下每帧做一次 GPU→CPU 传输，对解码帧率有一定影响。
```

### 6. 窗口 resize 适配（D3D SwapChain 模式）

```cpp
// D3D 模式：WM_SIZE 时即时 ResizeBuffers，防止 Qt 布局延迟导致黑帧
void YourWidget::nativeEvent(const QByteArray& eventType, void* message, long* result) {
    MSG* msg = static_cast<MSG*>(message);
    if (msg->message == WM_SIZE) {
        int w = LOWORD(msg->lParam), h = HIWORD(msg->lParam);
        if (w > 0 && h > 0) player_->resizeRender(w, h);
    }
    return QWidget::nativeEvent(eventType, message, result);
}

// GL 模式
void YourWidget::resizeEvent(QResizeEvent* event) override {
    QWidget::resizeEvent(event);
    if (glWidget_) {
        glWidget_->setGeometry(playAreaRect());
    }
}
```

### 7. D3D Overlay 文字/图片叠加

```cpp
// ── 创建文字叠加（open 成功后调用）──
int id = player->createTextOverlay("状态文本", "Microsoft YaHei", 18);
player->setTextOverlayColor(id, 0xFF00FF00);      // 绿色 (AARRGGBB)
player->setTextOverlayRect(id, {8, 8, 200, 36});  // 左上角 (逻辑像素)

// ── 动态更新文字 ──
player->updateTextOverlay(id, "CPU: 23.5%\nGPU: 45.2%");

// ── 创建图片叠加（Logo/水印）──
int imgId = player->createImageOverlay(64, 64, bgraData, stride);
player->setImageOverlayRect(imgId, {w-74, 10, w-10, 74});  // 右上角

// ── 销毁 ──
player->destroyTextOverlay(id);
player->destroyImageOverlay(imgId);
```

### 8. 画中画 (PIP)

```cpp
// ── 启用 PIP（D3D SwapChain 模式下有效）──
player->enablePip(true);
// 设置 PIP 矩形：右上角，逻辑像素坐标，内部自动按 DPI 缩放转物理像素
player->setPipRect({w-160, 10, w-10, 110});

// ── 禁用 PIP ──
player->enablePip(false);

// ── 拖拽支持（Demo 层实现）──
// 在 D3DRenderWidget 的 mouseMoveEvent 中更新 PIP 位置：
// player->setPipRect({newLeft, newTop, newRight, newBottom});
```

> **时序说明**：`enablePip()`/`setPipRect()` 可在 `openAsync()` 之前调用，SDK 内部使用
> `pendingPipState` 机制在 `D3D11FrameRenderer` 创建后自动应用。

### 9. D3D RenderWidget（纯 D3D11 HWND 控件）

```cpp
// D3DRenderWidget 属性设置（与 zeroD3DDemo 一致）
setAttribute(Qt::WA_NativeWindow);      // 创建独立 HWND
setAttribute(Qt::WA_PaintOnScreen, true);  // 禁用 Qt backing store
setAttribute(Qt::WA_OpaquePaintEvent, true);
setAttribute(Qt::WA_NoSystemBackground, true);
setMouseTracking(true);                  // PIP 悬停光标切换

// paintEngine() 返回 nullptr，彻底禁止 Qt GDI 绘制覆盖 D3D 内容
QPaintEngine* paintEngine() const override { return nullptr; }
```

### 10. Qt 控件叠加 QOpenGLWidget

```cpp
// ✅ 正确 — parent 必须是 GL Widget
auto label = new QLabel(glWidget);
label->setAttribute(Qt::WA_TranslucentBackground);
label->setStyleSheet("QLabel { color: white; background: transparent; }");
label->move(10, 10);
label->show();
label->raise();
```

### 11. 入口 main.cpp 模板

```cpp
#include <QApplication>
#include <QSurfaceFormat>

// 强制独显（双显卡笔记本）
extern "C" {
    __declspec(dllexport) DWORD NvOptimusEnablement = 0x00000001;
    __declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}

int main(int argc, char* argv[]) {
    // sRGB 色彩空间（避免画面偏暗）
    QSurfaceFormat fmt;
    fmt.setSwapInterval(1);
    fmt.setColorSpace(QSurfaceFormat::sRGBColorSpace);
    QSurfaceFormat::setDefaultFormat(fmt);

    QApplication app(argc, argv);
    MainWindow w;
    w.show();
    return app.exec();
}
```

## 限制与前提

| 条件 | 说明 |
|------|------|
| **硬解+D3D 直渲** | 需要 NVIDIA GPU（D3D11VA + SwapChain） |
| **硬解+GL 互报** | 需要 NVIDIA GPU + WGL_NV_DX_interop 扩展 |
| **软解+D3D 直渲** | 任意支持 D3D11 的显卡（D3D11VA 不需要） |
| **软解+GL Shader** | 任意支持 OpenGL 3.3 Core 的显卡 |
| **D3D Overlay** | 仅 D3D SwapChain 模式支持（GDI 渲染到 D3D11 纹理） |
| **画中画 (PIP)** | 仅 D3D SwapChain 模式支持，坐标使用逻辑像素+DPI 缩放 |
| **高 DPI** | 需开启 `Qt::AA_EnableHighDpiScaling`，Overlay/PIP 坐标自动按 DPI 缩放 |
| **HTTP-FLV** | 支持 HTTPS（cpr + OpenSSL），自签名证书需跳过验证 |
| **HEVC-in-FLV** | FFmpeg 5.1.2+ 支持 HEVC 解码，FLV 码流为 length-prefix 格式 |
| **音频编解码** | AAC / Opus / G.711A / G.711U（FLV 和 FFmpeg 路径均支持） |
| **色彩空间** | 自动检测 BT.601/BT.709/BT.2020（根据 AVFrame colorspace） |
| **编码支持** | H.264 / H.265（FFmpeg codec） |
| **录制** | libx264 编码为 H.264 MP4（仅视频） |
| **播放控制** | `pause()` / `replay()` 当前为空实现 |

## 相关文档

- [WIKI.md](WIKI.md) — 详细技术文档（缺陷记录、踩坑经验、WGL 互操作细节）
