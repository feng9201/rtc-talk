# mtAudio — 统一音频解码库

## 概述

mtAudio 是 MuxPlayer 项目的统一音频解码库，封装 AAC / Opus / G.711 三种主流音频编解码格式，对外提供简洁一致的 C++ 接口。  
支持可选的 PCM 重采样功能（CMake 宏 `MT_AUDIO_ENABLE_RESAMPLE`），开启后将所有解码输出统一转换为 **16000 Hz / 单声道 / 16-bit signed**，便于下游语音识别或音频分析模块直接使用。

| 属性 | 值 |
|------|-----|
| 命名空间 | `mtAudio` |
| 语言标准 | C++17 |
| 构建产物 | `mtAudio.lib`（静态库） |
| 依赖库 | fdk-aac · libopus · miniaudio |
| 支持格式 | AAC-LC · Opus · G.711 A-law · G.711 μ-law |
| 重采样目标 | 16000 Hz / mono / 16-bit signed（可选） |

---

## 目录结构

```
mtAudio/
├── CMakeLists.txt              # 构建配置
├── wiki.md                     # 本文档
│
├── include/
│   └── mt_audio_decoder.h      # 公共 API 头文件（外部唯一引用点）
│
├── src/
│   ├── decoder_internal.h      # 内部解码器类声明（包内可见）
│   ├── decoder_factory.cpp     # AudioDecoder::create() 工厂实现
│   ├── aac_decoder.cpp         # AAC 解码（fdk-aac）
│   ├── opus_decoder.cpp        # Opus 解码（libopus）
│   ├── g711_decoder.cpp        # G.711 A-law / μ-law 解码
│   ├── audio_resampler.h       # 重采样器头文件
│   └── audio_resampler.cpp     # 重采样器实现（miniaudio）
│
├── g711/                       # G.711 原始 C 实现（参考 demo）
│   ├── g711.h
│   ├── g711.c
│   ├── encodeG711.h
│   └── encodeG711.cpp
│
├── opusEnc/                    # Opus 编解码参考 demo
│   ├── opusEncoder.h
│   ├── opusEncoder.cpp
│   ├── opusDecoder.h
│   └── opusDecoder.cpp
│
└── resample/                   # miniaudio 重采样参考 demo
    ├── miniResampling.h
    └── miniResampling.cpp
```

---

## 架构设计

```
外部调用方
    │
    │  AudioDecoder::create(AudioCodec::AAC)
    ▼
┌──────────────────────────────────────┐
│         AudioDecoder（抽象基类）       │
│  ┌───────────┐ ┌──────────┐ ┌──────┐ │
│  │ AacDecoder│ │OpusDecoder│ │G711  │ │
│  │ (fdk-aac) │ │ (libopus) │ │(g711.c)│
│  └─────┬─────┘ └─────┬────┘ └──┬───┘ │
│        └──────────┬──┴─────────┘      │
│              AudioResampler           │
│         (miniaudio ma_data_converter) │
└──────────────────────────────────────┘
```

### 核心设计原则

1. **单一公共头文件**：外部仅需 `#include "mt_audio_decoder.h"`，无需接触任何内部实现细节。
2. **工厂模式**：`AudioDecoder::create(codec)` 按编解码类型返回对应子类，调用方持有 `std::unique_ptr<AudioDecoder>` 即可。
3. **PTS 透传**：`decode()` 接收 `int64_t pts`（毫秒），原样写入 `AudioFrame.pts`，下游直接用于音视频同步。
4. **重采样可选**：`MT_AUDIO_ENABLE_RESAMPLE` 宏编译期控制；若输入已为 16000 Hz / mono / 16-bit，则自动跳过重采样，零开销。

---

## 公共 API

### AudioCodec 枚举

```cpp
namespace mtAudio {

enum class AudioCodec {
    AAC,     // MPEG-4 AAC-LC
    Opus,    // Opus
    G711A,   // G.711 A-law
    G711U,   // G.711 μ-law
};

}
```

### AudioFrame 结构体

解码后的一帧 PCM 数据：

```cpp
struct AudioFrame {
    std::vector<uint8_t> data;   // PCM 原始字节
    int   sampleRate    = 0;     // 采样率 (Hz)
    int   channels      = 0;     // 声道数
    int   bitsPerSample = 0;     // 位深 (固定 16)
    int   numSamples    = 0;     // 每声道样本数
    int64_t pts         = -1;    // 展示时间戳 (毫秒)，-1 表示无 PTS

    bool empty() const { return data.empty(); }
};
```

| 字段 | 说明 |
|------|------|
| `data` | PCM 字节，字节序为小端（与本机一致），位深固定 16-bit signed |
| `sampleRate` | 输出采样率；重采样关闭时为原始采样率，开启时为 16000（若输入已匹配则保持原值） |
| `channels` | 输出声道数；重采样关闭时为原始声道数，开启时为 1（若输入已匹配则保持原值） |
| `bitsPerSample` | 固定 16（所有解码器输出均为 `int16_t`） |
| `numSamples` | 每声道样本数，`data.size() == numSamples * channels * 2` |
| `pts` | 毫秒级展示时间戳，由 `decode()` 透传，供音视频同步使用 |

### AudioDecoder 抽象基类

```cpp
class AudioDecoder {
public:
    // 工厂：创建对应编解码类型的解码器
    static std::unique_ptr<AudioDecoder> create(AudioCodec codec);

    // 初始化（必须在 decode 前调用）
    // extraData: AAC = AudioSpecificConfig, Opus/G.711 = 可传空
    // sampleRate: 0 = 由 extraData 推断（Opus 默认 48000）
    // channels: 声道数（Opus 默认 2）
    virtual bool init(const uint8_t* extraData, int extraDataLen,
                      int sampleRate = 0, int channels = 1) = 0;

    // 解码一帧压缩数据 → PCM
    // pts: 毫秒，无 PTS 传 -1
    // 返回每声道样本数，< 0 表示错误
    virtual int decode(const uint8_t* data, int len,
                       AudioFrame& outFrame, int64_t pts = -1) = 0;

    // 重置解码器状态（seek 或切换流后调用）
    virtual void reset() = 0;

    // 查询输出格式
    int outputSampleRate() const;
    int outputChannels()   const;
};
```

---

## 各解码器说明

### AAC（fdk-aac）

| 项目 | 说明 |
|------|------|
| 底层库 | Fraunhofer FDK AAC（`fdk-aac`） |
| 传输类型 | `TT_MP4_RAW`（raw AAC frames，非 ADTS） |
| extradata | 必须传入 **AudioSpecificConfig**（通常 2 字节），来自 FLV/MP4 容器 |
| 输出位深 | 16-bit signed（`INT_PCM` = `short`） |
| 帧大小 | AAC-LC 每帧 1024 samples/声道 |
| 解码缓冲区 | 8192 个 `INT_PCM`（容纳 1024 samples × 8 声道最大值） |

**典型流程**（从 FLV demux 获取 AAC 包）：

```cpp
auto dec = mtAudio::AudioDecoder::create(mtAudio::AudioCodec::AAC);

// extraData = AudioSpecificConfig，通常来自 FLV AudioTagHeader 后 2 字节
dec->init(audioSpecificConfig, 2, 0, 0);

// 每次收到 AAC raw frame
mtAudio::AudioFrame frame;
int samples = dec->decode(packetData, packetLen, frame, pts_ms);
if (samples > 0) {
    // frame.data 包含 16-bit PCM
    // frame.pts == pts_ms
}
```

---

### Opus（libopus）

| 项目 | 说明 |
|------|------|
| 底层库 | libopus（`Opus::opus`） |
| extradata | 可为空（Opus in FLV 无专用 config） |
| 默认采样率 | 48000 Hz（`sampleRate` 传 0 时使用） |
| 默认声道数 | 2（stereo，`channels` 传 0 时使用） |
| 最大帧缓冲 | 5760 samples/声道（120 ms @ 48 kHz） |

**典型流程**：

```cpp
auto dec = mtAudio::AudioDecoder::create(mtAudio::AudioCodec::Opus);
dec->init(nullptr, 0, 48000, 2);

mtAudio::AudioFrame frame;
dec->decode(opusPacket, opusLen, frame, pts_ms);
```

---

### G.711 A-law / μ-law

| 项目 | 说明 |
|------|------|
| 底层实现 | 项目内 `g711.c`（ITU-T 标准 companding 表） |
| extradata | 无需（固定格式） |
| 默认采样率 | 8000 Hz |
| 默认声道数 | 1（mono） |
| 解码规则 | 每个压缩字节 → 1 个 `int16_t` 线性 PCM 样本 |

**典型流程**：

```cpp
auto dec = mtAudio::AudioDecoder::create(mtAudio::AudioCodec::G711A);
dec->init(nullptr, 0, 8000, 1);

mtAudio::AudioFrame frame;
dec->decode(g711Data, g711Len, frame, pts_ms);
// frame.numSamples == g711Len（1 byte → 1 sample）
```

---

## 重采样模块

### AudioResampler

基于 **miniaudio** 的 `ma_data_converter` 封装，专用于将任意 PCM 格式转换为固定目标格式。

| 目标参数 | 值 |
|----------|-----|
| 采样率 | 16000 Hz |
| 声道数 | 1（单声道） |
| 位深 | 16-bit signed（`ma_format_s16`） |

**智能跳过**：若输入格式已完全匹配目标（16000 Hz / mono / 16-bit），`needsResample()` 返回 `false`，`resample()` 直接复制数据，不创建 `ma_data_converter` 实例。

**使用场景**：语音识别（ASR）模块通常要求 16 kHz mono 16-bit 输入，开启重采样后所有音频解码输出可直接送入 ASR，无需下游再做格式转换。

### 宏开关

```cmake
# CMakeLists.txt（默认关闭）
option(MT_AUDIO_ENABLE_RESAMPLE "Enable resampling to 16000Hz/mono/16bit" OFF)
```

```bash
# 启用重采样
cmake -B build -DMT_AUDIO_ENABLE_RESAMPLE=ON
```

宏生效时，编译定义 `MT_AUDIO_ENABLE_RESAMPLE` 被添加到 `mtAudio` 的 `PUBLIC` 定义中，所有链接 mtAudio 的目标均可感知该宏。

---

## PTS 与音视频同步

### PTS 来源

| 来源 | 说明 |
|------|------|
| `FFmpegDemuxing` | 解封装时将 `AVPacket.pts` 转换为毫秒后传入 |
| `FlvDemuxing` | FLV Tag Timestamp（毫秒）直接作为 PTS |
| 外部调用 | 任何来源均可，只要传毫秒值 |

### 音视频同步策略

`AudioFrame.pts` 与视频帧 `VideoFrame.pts` 均为毫秒时间戳，下游同步逻辑：

```
audioPts - videoPts > 阈值  → 音频超前，延迟音频播放
videoPts - audioPts > 阈值  → 视频超前，跳帧或等待
|audioPts - videoPts| ≤ 阈值 → 同步正常
```

---

## CMake 集成

### 在根 CMakeLists.txt 中引入

```cmake
add_subdirectory(mtAudio)
```

### 其他模块链接 mtAudio

```cmake
target_link_libraries(yourTarget PRIVATE mtAudio)
```

链接后自动获得：
- `mtAudio/include` 公共头文件目录（`PUBLIC`）
- `fdk-aac` 和 `Opus::opus` 传递依赖（`PUBLIC`）
- `MT_AUDIO_ENABLE_RESAMPLE` 宏定义（若开启）

### vcpkg 依赖

```json
// vcpkg.json
{
  "dependencies": [
    "fdk-aac",
    "opus",
    "miniaudio"
  ]
}
```

---

## 完整使用示例

### 在 MuxPlayer 解封装回调中解码音频

```cpp
#include "mt_audio_decoder.h"

// 初始化解码器（收到第一个音频包时）
std::unique_ptr<mtAudio::AudioDecoder> audioDec;

void onAudioConfig(mtAudio::AudioCodec codec,
                   const uint8_t* extraData, int extraLen,
                   int sampleRate, int channels)
{
    audioDec = mtAudio::AudioDecoder::create(codec);
    if (!audioDec || !audioDec->init(extraData, extraLen, sampleRate, channels)) {
        fprintf(stderr, "Audio decoder init failed\n");
    }
}

// 每收到一个音频压缩 packet
void onAudioPacket(const uint8_t* data, int len, int64_t ptsMs)
{
    if (!audioDec) return;

    mtAudio::AudioFrame frame;
    int samples = audioDec->decode(data, len, frame, ptsMs);
    if (samples <= 0) return;

    // frame.data    : 16-bit PCM 字节
    // frame.pts     : 毫秒时间戳（用于 AV sync）
    // frame.sampleRate / channels / numSamples
    sendToAudioOutput(frame);
}

// seek 后重置
void onSeek()
{
    if (audioDec) audioDec->reset();
}
```

---

## 错误处理

| 错误码 / 返回值 | 含义 |
|----------------|------|
| `init() = false` | 初始化失败（参数错误 / 库初始化失败） |
| `decode() < 0` | 解码错误（数据损坏 / 解码器内部错误） |
| `decode() = 0` | 未解码出数据（AAC 缓冲未满 / Opus DTX 静音帧） |
| `decode() > 0` | 成功，返回每声道样本数 |

所有错误均通过 `fprintf(stderr, ...)` 输出日志，前缀为解码器名称（`[AacDecoder]` / `[OpusDecoder]` / `[G711Decoder]`）。

---

## 扩展新编解码器

如需新增格式（如 MP3、Vorbis），只需：

1. 在 `AudioCodec` 枚举中添加新值。
2. 在 `decoder_internal.h` 中声明新的子类。
3. 新建 `src/xxx_decoder.cpp` 实现 `init()` / `decode()` / `reset()`。
4. 在 `decoder_factory.cpp` 的 `create()` 中添加 `case` 分支。
5. 在 `CMakeLists.txt` 的 `MTAUDIO_SRCS` 中添加新源文件。

无需修改任何调用方代码，工厂模式保证了开闭原则。

---

## 与参考 demo 的关系

| demo 目录 | 用途 | mtAudio 中的对应 |
|-----------|------|-----------------|
| `g711/` | G.711 A-law/μ-law 编解码参考实现 | `src/g711_decoder.cpp` 调用 `g711/g711.c` |
| `opusEnc/` | Opus 编解码参考实现 | `src/opus_decoder.cpp` 使用 libopus API |
| `resample/` | miniaudio 重采样参考实现 | `src/audio_resampler.cpp` 使用 `ma_data_converter` |

demo 目录中的文件**不参与 mtAudio 编译**（CMakeLists.txt 未引用），仅作为算法参考保留。

---

## 已知限制

| 限制 | 说明 |
|------|------|
| AAC 仅支持 raw 模式 | 不支持 ADTS 格式 AAC（`TT_MP4_RAW` 模式），ADTS 需自行剥离头部后传入 |
| 重采样目标固定 | 目标格式硬编码为 16000 Hz / mono / 16-bit，如需其他目标需修改 `audio_resampler.cpp` 中的常量 |
| 线程安全 | 同一解码器实例非线程安全，需调用方保证同一时刻只有一个线程调用 `decode()` |
| Opus FEC 未启用 | `opus_decode()` 的 `decode_fec` 参数固定为 0，不支持前向纠错 |
