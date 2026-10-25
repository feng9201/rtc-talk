# libwebrtc M146 增加 H.264 / H.265 支持方案

> 基线：`D:\google\webrtc\src` @ `12cce13d`（branch-heads/7680，M146），编译记录见 `D:\google\webrtc.md`，现有交付物 `D:\google\webrtc_sdk`。

## 1. 现状核查结论

| 项 | 现状 | 依据 |
|---|---|---|
| 构建目录 | `out\win_x64_{debug,release}` 保留着上次的构建，可增量编译 | 2026-10-13 已增量重编完成，见第 8 节 |
| 本地源码改动 | 仅 `BUILD.gn` 的 AV1 门控（13+/6-），保留 | `git diff --stat` |
| H.264 RTP 打包/解包、SDP 协商 | **已编译**，不受开关影响 | `modules/rtp_rtcp` 无条件编入 `rtp_format_h264` |
| H.264 软件编解码器 | 未编译（`rtc_use_h264=false`） | 开启后为 OpenH264 编码 + FFmpeg 解码，`modules/video_coding/BUILD.gn:501` |
| H.265 RTP 打包/解包 | **未编译**（SDK 里只有头文件，没有实现） | `modules/rtp_rtcp/BUILD.gn:262` 受 `rtc_use_h265` 控制 |
| H.265 SDP 协商、码流解析 | 未编译，`RTC_ENABLE_H265` 未定义 | `BUILD.gn:324`、`common_video`、`api/video_codecs` 均受门控 |
| H.265 编解码器 | **libwebrtc 任何版本都不自带** | 源码 `modules/video_coding/codecs` 只有 av1/h264/vp8/vp9 |
| `rtc_use_h265` 默认值 | 跟随 `proprietary_codecs`（独立编译时为 false） | `webrtc.gni:183` |
| macOS VideoToolbox | `sdk/objc` 只有 H.264 实现，无 H.265 | `sdk/objc/components/video_codec` |

结论：**传输层只需改编译参数；编解码器必须自己实现（H.265 必然如此，H.264 建议如此）。**

## 2. 方案选择

### 方案 A（推荐）：libwebrtc 只开 H.265 传输，H.264/H.265 编解码器全部外部注入

```gn
rtc_use_h265 = true      # 新增：H.265 打包/解包、SDP、码流解析
rtc_use_h264 = false     # 保持：不编入 OpenH264 / 内置 FFmpeg
```

- 编解码器在 talk-core 中按平台实现，通过 `VideoEncoderFactory` / `VideoDecoderFactory` 注入。
- 优点：
  - libwebrtc 内部不带 FFmpeg，**不会和自己用的 FFmpeg 发生符号冲突**（同名的 `avcodec_*` 一个在 `webrtc.lib` 里、一个在自建 FFmpeg 里，链接时会重复定义或静默用错版本）；
  - 硬件编解码是对讲视频的主路径（RK 只能硬编，桌面端也应优先硬件），反正要自己写工厂；
  - 授权清晰：不分发未授权的 OpenH264 源码编译产物。
- 代价：没有"开箱即用"的软件 H.264，需要在工厂里自己做软件兜底。

### 方案 B：同时开启内置软件 H.264

```gn
rtc_use_h265 = true
rtc_use_h264 = true
proprietary_codecs = true
ffmpeg_branding = "Chrome"     # 内置 FFmpeg 必须带 H.264 解码器，否则 H264DecoderImpl 初始化失败
```

- 适合：短期内想先用软件 H.264 跑通视频，且应用里**不再链接任何其他 FFmpeg**。
- 注意：
  - Chromium 内置的 FFmpeg 不含 HEVC 解码器，**方案 B 也解决不了 H.265 编解码**；
  - 从源码编译的 OpenH264 **不享受** Cisco 的专利授权（只有 Cisco 官方分发的二进制才有）；
  - 后续接自建 FFmpeg 做硬件编解码时会遇到上面说的符号冲突，需要回到方案 A。

**建议采用方案 A。**

## 3. 重编步骤（Windows x64）

### 3.1 环境

沿用上次的经验（`webrtc.md` 问题表 #3~#5）：

```bat
set PATH=D:\google\depot_tools;%PATH%
set DEPOT_TOOLS_WIN_TOOLCHAIN=0
set vs2022_install=C:\Program Files\Microsoft Visual Studio\2022\Community
set HTTP_PROXY=http://127.0.0.1:7897
set HTTPS_PROXY=http://127.0.0.1:7897
```

源码不需要重新同步（`rtc_use_h265` 不引入新依赖）。

### 3.2 args.gn

直接写文件再 `gn gen`（不用 `--args`，避免 cmd 吃掉引号）。

`out\win_x64_release\args.gn`：

```gn
target_os = "win"
target_cpu = "x64"
is_debug = false
symbol_level = 1
is_component_build = false
is_clang = true
use_custom_libcxx = false
use_rtti = true
rtc_include_tests = true
rtc_build_examples = false
rtc_build_tools = false
treat_warnings_as_errors = false
enable_libaom = false
rtc_include_dav1d_in_internal_decoder_factory = false

rtc_use_h264 = false
rtc_use_h265 = true
```

Debug 同上，改 `is_debug = true` 并去掉 `symbol_level`。

### 3.3 编译

```bat
cd /d D:\google\webrtc\src
gn gen out\win_x64_release
gn gen out\win_x64_debug
autoninja -C out\win_x64_release webrtc
autoninja -C out\win_x64_debug webrtc
```

官方 `webrtc_lib_link_test` 在无 AV1 配置下不可链接（`webrtc.md` 第 7 节），继续用 SDK 的 `peerconnection_smoke` 示例验证。

### 3.4 验证

| 检查 | 方法 | 预期 |
|---|---|---|
| 宏生效 | `gn desc out\win_x64_release //:webrtc defines` | 含 `RTC_ENABLE_H265` |
| H.265 传输已编入 | `dumpbin /symbols obj\webrtc.lib \| findstr RtpPacketizerH265` | 有符号 |
| H.265 协商已编入 | 同上查 `H265GenerateProfileTierLevelForAnswer` | 有符号 |
| 无 FFmpeg/OpenH264 | 同上查 `avcodec_` / `WelsCreateSVCEncoder` | 无符号 |
| 示例 | `peerconnection_smoke` Debug/Release | exit 0 |
| 协商 | 示例中注入只声明 H264/H265 格式的假工厂，两个 PC 本地 offer/answer | SDP 中出现 `H264`、`H265` 的 rtpmap，answer 协商成功 |

### 3.5 更新 SDK 交付

1. 复制两个配置的 `webrtc.lib`，用 `D:\google\webrtc\dll_build\make_webrtc_dll.ps1` 重新生成 `webrtc.dll`；`make_standalone_libs.ps1` 无需重跑（三方库未变）。
2. **消费方宏清单加入 `RTC_ENABLE_H265`**（`manifest\desc_defines_*.txt`、`peerconnection_smoke.vcxproj`、`usage.md`）。头文件里有受该宏控制的声明，库与消费方不一致会出现未解析符号或 ODR 问题。
3. 头文件包重新导出一次（`common_video\h265`、`modules\rtp_rtcp\source\*h265*` 已在，但以重编后的 `gen` 目录为准）。
4. `manifest\build_info.txt` 与 `usage.md` 的"编解码"说明改为：H.264/H.265 传输已启用，编解码器需外部注入。
5. 建议另存为新目录 `webrtc_sdk_hevc`，与现有 SDK 并行保留，验证通过后再替换。

## 4. 外部编解码器实现（talk-core 内）

### 4.1 统一抽象

```
talk::VideoEncoderFactory  (webrtc::VideoEncoderFactory)
  ├─ GetSupportedFormats()：启动时探测硬件能力，按优先级返回
  │    H264 (profile 42e01f/640c1f, packetization-mode=1)
  │    H265 (仅当本机有 H.265 硬编)
  │    VP8 (libvpx 软编兜底，已在 SDK 内)
  └─ Create(format) → 平台后端

talk::VideoDecoderFactory 同理（H.265 解码能力单独探测）
```

各后端实现 `webrtc::VideoEncoder` 的要点：

- `InitEncode`：低延迟配置——**无 B 帧**、CBR/受限 VBR、长 GOP + 按需 IDR、每个 IDR 前重复 SPS/PPS（H.265 还有 VPS）。
- `SetRates`：跟随 libwebrtc 拥塞控制（GCC/TWCC）给出的码率实时调整。
- `Encode`：收到 `kVideoFrameKey` 请求立即出 IDR；输出 Annex-B 码流 + `CodecSpecificInfo`。
- `GetEncoderInfo`：`is_hardware_accelerated=true`、分辨率对齐（硬编通常 16）、`implementation_name` 便于排查。
- 输入：优先 NV12/原生纹理缓冲，避免 I420↔NV12 来回转换（libyuv 已在 SDK 内）。

### 4.2 平台后端

| 平台 | H.264 | H.265 | 实现方式 |
|---|---|---|---|
| Windows | 硬编：NVENC / QSV / AMF / Media Foundation；硬解：D3D11VA | 同左（需 GPU 支持 HEVC） | **自建 FFmpeg（LGPL，`--disable-gpl`，动态库）**统一调用：`h264_nvenc/hevc_nvenc`、`h264_qsv/hevc_qsv`、`h264_amf/hevc_amf`、`h264_mf/hevc_mf`，解码用 `d3d11va` 硬件加速 + FFmpeg 原生软件解码兜底 |
| macOS | VideoToolbox | VideoToolbox（Apple Silicon / 较新 Intel 均有硬件 HEVC） | H.264 参考 `sdk/objc/components/video_codec/RTCVideoEncoderH264.mm` 改写为纯 C++；H.265 照此新增（`kCMVideoCodecType_HEVC`） |
| RK3568 / RK3576 | MPP 硬编解码 | MPP 硬编解码 | 直接调 `librockchip_mpp`（`MPP_VIDEO_CodingAVC/HEVC`），零拷贝用 DMA-BUF；不经 FFmpeg |

软件兜底：
- H.264 编码：Windows/macOS 无硬编时用 OpenH264，**运行时下载 Cisco 官方二进制**以获得专利授权；不用 libx264（GPL）。
- H.265 编码：不做软件兜底（x265 是 GPL 且 CPU 开销大）；没有硬编的设备不声明 H.265，协商自动落到 H.264。
- 解码：FFmpeg 原生 `h264`/`hevc` 软件解码器是 LGPL，可作为桌面端兜底。

### 4.3 Windows 上 H.265 的额外注意

- Media Foundation 的 HEVC 解码 MFT 依赖系统里的"HEVC 视频扩展"（应用商店组件，部分机器没有）；**走 FFmpeg + D3D11VA 直接调 GPU 解码可绕开这个依赖**。
- 核显/独显型号差异大，启动时必须实际创建一次编码器探测，而不是只看驱动版本。

## 5. 与对讲方案的衔接

- 信令 `session.hello` 的 `caps.video.codecs` 上报探测结果（含 `hw` 标记），见 [协议 9.2](./talk-protocol.md)。
- 编码优先级：H.264 硬编 > H.265（双方都有硬件时）> VP8 软编。
- **mediasoup 3.30.1 不支持 H.265**（2026-10-24 容器实测：Router 注册 `video/H265` 直接报 `media codec not supported`；上游 CHANGELOG 3.16.6 "Remove H265 codec"，PR #1564，worker 侧 H.265 帧解析/关键帧识别已删除，NACK/PLI/requestKeyFrame 均不可用）。**SFU 房间视频 = H.264 单层**，H.265-only offer 服务端返回 501（`sdp.ts` parseSdp）。
- **H.265 仅 P2P 直连可用**（`p2p.offer/answer` 透明转发，服务端零参与）：双方都有 H.265 硬件时可用，任一方没有则 SDP 协商自然回落 H.264；第三人入房切 SFU 后新 PC 重新以 H.264 协商（模式切换本就重建 PC，无需额外处理）。
- 上游若恢复 H265，服务端需同步改四处：`codecs.ts` 注册 codec、`sdp.ts` parseSdp 匹配 h265 并去掉 501、`buildSdp` 的 rtpmap 按 codec 写（现为 `H264`/`opus` 硬编码）、`room-media.ts` `recvCaps` 放行，约 1 天。不建议 fork worker 补齐：关键帧识别被删，弱网下重传/关键帧恢复全废。
- 后续浏览器接入：Chrome 136+ 在有硬件支持时才协商 H.265，必须保留 H.264。

## 6. 其他平台

- **Linux arm64（RK3568/3576）**：同一份源码交叉编译，args 增加 `target_os="linux" target_cpu="arm64"`，`rtc_use_h265=true`、`rtc_use_h264=false` 相同；sysroot 与板卡系统 glibc 对齐（计划 P0 项）。
- **macOS**：在 Mac 上用同一分支编译，`target_os="mac"`，参数同上。

## 8. 执行结果（2026-10-13，方案 A）

- 产物：`D:\google\webrtc_sdk_hevc`（原 `D:\google\webrtc_sdk` 未改动）。
- `webrtc.lib`：Release 150.0 MB、Debug 654.7 MB；含 H.265 打包/解包、协商、码流解析符号；无 FFmpeg/OpenH264/AV1 符号。
- `webrtc.dll`：20.6 MB，导出 61273 项。启用 H.265 后全量导出超过 PE 上限 65535，已在脚本中排除 perfetto 符号。
- 验证：`examples\h265_negotiation` 在 ClangCL Debug/Release 和 cl.exe + DLL 三种组合下均 PASSED，offer/answer 均含 `H264/90000` 与 `H265/90000`。
- 消费方新增宏 `RTC_ENABLE_H265`。
- 详细记录见 `D:\google\webrtc.md` 第 8 节。

## 7. 工作量与风险

| 工作 | 估计 |
|---|---|
| Windows 重编 + SDK 更新 + 协商验证 | 1~2 天（主要是全量编译时间） |
| Windows FFmpeg 后端（NVENC/QSV/AMF/MF + D3D11VA） | 1~2 周 |
| macOS VideoToolbox H.264/H.265 | 1 周 |
| RK MPP H.264/H.265 | 1~2 周（含零拷贝与板卡实测） |
| 三端互通与弱网测试 | 1 周 |

| 风险 | 对策 |
|---|---|
| `rtc_use_h265=true` 在 `use_custom_libcxx=false` 下出现编译问题 | 先编 Release 验证；问题通常是个别头文件缺 include，打小补丁并记录到 `webrtc.md` |
| 硬编码器对码率突变响应慢、IDR 延迟大 | 编码器参数调优；必要时限制 `SetRates` 调整频率 |
| H.264/H.265 专利费 | 优先依赖硬件（芯片/系统厂商已授权）；软件编码只用 Cisco OpenH264 二进制；商用前法务确认 |
