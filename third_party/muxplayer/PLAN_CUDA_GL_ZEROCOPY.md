# Plan：N 卡硬解（nvcodec/CUDA）+ OpenGL 近零拷贝

> 状态：**暂不执行**，仅文档存档。  
> 日期：2026-08-21  
> 依赖前提：FFmpeg 7.1.4 已启用 `nvcodec`（见仓库 `vcpkg.json`）。

## 已确认决策

| # | 议题 | 结论 |
|---|------|------|
| 1 | 后端选择 | **显式调用**：通过 API（如 `setHwAccelBackend(CUDA)`）开启，不默认静默切 CUDA |
| 2 | 零拷贝程度 | **可接受 1 次 GPU blit**（device→mapped GL），不做真零拷贝帧池 |
| 3 | 本机构建 | **不装 CUDA Toolkit**（本机无 N 卡）；`MUXPLAY_ENABLE_CUDA` **默认 OFF**，有 Toolkit/N 卡的机器再打开 |
| 4 | inject / 摄像头 Live | **可以**优先走 CUDA + GL（显式开启后） |

---

## 目标

在现有 `MultiplexPlayer` 上增加一条路径：

**NVDEC（FFmpeg `AV_HWDEVICE_TYPE_CUDA` / nvcodec）硬解 → 至多 1 次 GPU 拷贝 → OpenGL 显示**，全程无 CPU 像素回读。

与现有 **D3D11VA + SwapChain / WGL** 路径并存；CUDA 路径需显式打开。

---

## 现状 vs 目标

| | 当前硬解 + GL | 目标 CUDA + GL |
|--|---------------|----------------|
| 解码 | D3D11VA → `AV_PIX_FMT_D3D11` | CUDA → `AV_PIX_FMT_CUDA` |
| 色彩转换 | D3D PS：NV12 → 共享 RGBA | GL YUV Shader（复用 SoftGL） |
| 到 GL | `WGL_NV_DX_interop` | `cudaGraphicsGLRegister*` + `cudaMemcpy2D` |
| CPU | 无 | 无 |
| GPU 额外拷贝 | NV12→NV12 Copy + D3D 转 RGBA | **允许 1 次** D2D blit 到 GL 纹理 |
| Overlay / PIP | GL 路径不支持 | 同样不支持 |

说明：FFmpeg `hw_frames_ctx` 管理解码表面，难以让 NVDEC 直接写入 GL 纹理；工程方案定为「近零拷贝」：`cudaMemcpy2D`（CUDA 帧 → map 后的 GL 纹理）+ YUV→RGB shader。

---

## 推荐架构

```
DecodeMode::Hardware
  └─ HwAccelBackend（显式设置）
       ├─ D3D11VA  → D3D11FrameRenderer → SwapChain / WGL（现有，默认硬解）
       └─ CUDA     → CudaFrameBridge    → CudaGLDisplay → GLPlayWidget（新建）
```

```mermaid
flowchart LR
  demux[Demux] --> pktQ[vid_packetQ]
  pktQ --> cudaDec[CudaHardwareDecoder]
  cudaDec --> frameQ[decoded_frameQ]
  frameQ --> bridge[CudaFrameBridge]
  bridge --> glDisp[CudaGLDisplay]
  glDisp --> qw[GLPlayWidget]
```

数据流：

```
Packet
  → avcodec (h264/hevc) + AV_HWDEVICE_TYPE_CUDA
  → AVFrame (AV_PIX_FMT_CUDA, sw_format=NV12)
  → RenderScheduler（沿用 AV / PTS 壁钟 / Live）
  → CudaFrameBridge：持有最新 CUDA 帧（refcount）
  → GL 线程：map 已注册的 Y/UV GL 纹理 ← cudaMemcpy2D（1 次 GPU blit）
  → SoftGL 同款 YUV→RGB shader 绘制
```

---

## API / 模块改动

### 1. 公共 API（显式调用）

```cpp
enum class HwAccelBackend { D3D11VA, CUDA };

// 必须在 open / openFlv / injectExternalStream 之前调用
void setHwAccelBackend(HwAccelBackend b);
HwAccelBackend hwAccelBackend() const;  // 默认 D3D11VA
```

用法示例：

```cpp
player->setDecodeMode(DecodeMode::Hardware);
player->setHwAccelBackend(HwAccelBackend::CUDA);  // 显式
player->setGLInteropMode(true);
// inject / openFlv / openAsync ...
```

- 未调用或设为 `D3D11VA`：行为与现网一致。  
- 设为 `CUDA` 但编译未开 `MUXPLAY_ENABLE_CUDA`，或运行时 init 失败：记录错误并走既定降级（见下）。

### 2. 解码：`decoder/cuda_decoder.h|cpp`

- `av_hwdevice_ctx_create(..., AV_HWDEVICE_TYPE_CUDA, ...)`
- `get_format` 选择 `AV_PIX_FMT_CUDA`
- 按 FFmpeg 7.1 实测决定是否手动创建 `hw_frames_ctx`
- 扩展 `frame_types.h`：`FrameMemoryType::CUDA_Device`
- 与现有 `HardwareDecoder` 并列，由 source / `setupRenderPipeline` 按 `HwAccelBackend` 选择

### 3. 消费与显示

| 新组件 | 职责 |
|--------|------|
| `CudaFrameBridge`（`IFrameConsumer`） | 类似 `FramePump`，保留最新 CUDA `AVFrame*` |
| `CudaGLInterop` | `cudaGraphicsGLRegisterImage`、map/unmap、`cudaMemcpy2D` |
| `CudaGLDisplay`（`IDisplayConsumer`） | GL Y/UV 纹理 + 复用 SoftGL YUV shader |

`GLPlayWidget`：按 `decodeMode` + `hwAccelBackend` 选择：

- `WGLInteropDisplay`（D3D11VA + GL）
- `SoftGLDisplay`（软解）
- `CudaGLDisplay`（CUDA + GL）

### 4. 管线装配（`setupRenderPipeline`）

```
Hardware + CUDA + GL:
  consumer_ = CudaFrameBridge
  不创建 D3D11FrameRenderer / Overlay
  HWND 可为 nullptr（inject + GL 场景）

Hardware + D3D11VA:
  保持现有逻辑
```

inject / 摄像头 Live：在调用方 **显式** `setHwAccelBackend(CUDA)` + `setGLInteropMode(true)` 后走本路径（决策 #4）。

### 5. 截图 / 录制

`SnapshotManager` / `RecordingManager` 增加 `AV_PIX_FMT_CUDA`：`av_hwframe_transfer_data` → CPU（与 D3D11 对称）。

### 6. 构建与依赖

| 项 | 约定 |
|----|------|
| CMake 选项 | `MUXPLAY_ENABLE_CUDA`，**默认 OFF** |
| 开启条件 | 检测到 CUDA Toolkit 且需要在 N 卡机器上构建时 `-DMUXPLAY_ENABLE_CUDA=ON` |
| 本机（无 N 卡） | 不安装 Toolkit，保持 OFF，不影响日常编译 |
| 编译依赖 | `cuda_runtime.h`、`cuda_gl_interop.h`，链接 `cudart` |
| 运行时 | NVIDIA 驱动；CUDA device 与 Qt GL 必须同一物理 GPU |
| vcpkg | 已有 `ffmpeg[nvcodec]@7.1.4`；**不足以**做 GL interop，仍需本机/CI 的 CUDA Toolkit（仅 ON 时） |

源码用 `#ifdef MUXPLAY_ENABLE_CUDA` 隔离，OFF 时无 CUDA 符号引用。

---

## 降级链

```
显式 CUDA 但 MUXPLAY_ENABLE_CUDA=OFF
  → 编译期无符号 / 或运行期拒绝，回退 D3D11VA（若 GL）或报错

CUDA init 失败（驱动/设备/context）
  → 优先回退 D3D11VA + 现有 WGL（若仍要 GL）
  → 再失败 → Software + SoftGL
```

D3D Overlay / PIP：**仅 D3D11VA + SwapChain**；CUDA+GL 明确不支持。

---

## 分阶段落地（执行时按此顺序）

| 阶段 | 内容 | 验收 |
|------|------|------|
| **P0 Spike** | 在有 N 卡 + CUDA Toolkit 的环境：FFmpeg 7.1.4 解一帧，确认 `AV_PIX_FMT_CUDA` | receive_frame 成功 |
| **P1 Decoder** | `CudaHardwareDecoder` + `CUDA_Device` + 接入 packet 队列 | 稳定出帧，尚无 GL |
| **P2 CUDA↔GL** | `CudaGLInterop` + `CudaGLDisplay`（1 次 GPU blit） | 画面正确；无 CPU transfer |
| **P3 接入 Player** | `setHwAccelBackend`、管线装配、`GLPlayWidget` 分支 | omni 侧显式打开后可用 |
| **P4 周边** | 截图/录制、降级、日志、README | URL / FLV / inject 冒烟 |
| **P5（可选）** | 多路共享 CUDA context、性能对比 | 延迟/占用报告 |

---

## 风险与约束

1. **双显卡 / Optimus**：CUDA device 与 GL context 必须同一 GPU；沿用现有强制独显导出。  
2. **线程模型**：建议解码线程只产出 `AVFrame`；**GL 线程**负责 map / `cudaMemcpy2D` / draw。  
3. **多实例**：多路 `MultiplexPlayer` 是否共享 CUDA primary context，在 P1 定案。  
4. **FFmpeg 7.1**：`hw_frames_ctx` 是否自动创建，P0 必须验证（与现有 D3D11 手动建池不同）。  
5. **与 WGL 并存**：CUDA 与 WGL 为两条 consumer/display，禁止混用同一帧路径。

---

## 明确不在本计划内

- 默认自动选用 CUDA  
- 真零拷贝自定义帧池 / EGLImage  
- 本机强制安装 CUDA Toolkit  
- CUDA 路径上的 Overlay / PIP  
- 替换或删除现有 D3D11VA 路径  

---

## 相关文档

- [README.md](README.md) — 现有四渲染模式与 AV/PTS/Live 调度  
- [AV_SYNC_REDESIGN.md](AV_SYNC_REDESIGN.md) — RenderScheduler 三层模型  
- [wiki.md](wiki.md) — D3D11VA / WGL 踩坑记录  
