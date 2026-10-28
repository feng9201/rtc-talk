# flvParser

轻量级 C++14 FLV（Flash Video）流解析库。支持从原始字节流中逐帧解析音视频数据，适用于 RTMP/HTTP-FLV 实时流场景。

## 特性

- **流式解析**：基于环形缓冲区，无需完整文件，支持网络字节流逐段输入
- **多编码支持**：H.264 (AVC)、H.265 (HEVC)、AAC
- **序列头提取**：自动缓存 Audio/Video Sequence Header，供推流端复用
- **Track ID 关联**：通过 Application Tag 将 track_id 绑定到对应视频帧
- **线程安全**：内部使用 moodycamel SPSC 有界队列，生产/消费线程解耦，零锁竞争
- **DLL 导出**：支持静态库与动态库两种构建模式

## 目录结构

```
flvparser/
├── CMakeLists.txt              # 顶层 CMake
├── Include/                    # 公开头文件
│   ├── FlvParser.h             # 主入口类
│   ├── info/FlvParserInfo.h    # Tag 类型 / Codec ID 枚举
│   └── packet/Packet.h         # 输出包结构体
├── src/                        # 内部实现
│   ├── CMakeLists.txt
│   ├── header/                 # FLV 文件头解析
│   ├── parser/                 # 核心解析器 (FlvParser.cpp)
│   ├── tag/                    # FLV Tag 解析
│   │   ├── tagData/            # 各类 TagData（Video/Audio/Script/Application）
│   ├── utils/                  # 环形缓冲区 + 字节序工具
│   └── packet/                 # Packet 实现
└── test/                       # 示例测试程序
    ├── CMakeLists.txt
    └── main.cpp                # HTTP-FLV 拉流解析示例
```

## 构建

### 依赖

| 依赖 | 用途 | 备注 |
|---|---|---|
| spsc (moodycamel) | SPSC 有界队列 `BlockingReaderWriterCircularBuffer` | 仅 header-only，通过 CMake 变量 `SPSC_INCLUDE_DIR` 指定路径 |
| mttool | 线程工具库 | 通过 vcpkg 或手动集成 |
| cpp-httplib | 测试程序 HTTP 客户端 | 仅 test 目标需要 |

### CMake 选项

| 选项 | 默认值 | 说明 |
|---|---|---|
| `FLVPARSER_BUILD_TEST` | `OFF` | 构建测试程序 |
| `FLVPARSER_BUILD_STATIC` | `ON` | 构建静态库 |
| `FLVPARSER_BUILD_SHARED` | `OFF` | 构建动态库 |

### 构建命令

```bash
# 静态库
cmake -B build -S .
cmake --build build --config Release

# 静态库（指定 spsc 路径）
cmake -B build -S . -DSPSC_INCLUDE_DIR=/path/to/spsc
cmake --build build --config Release

# 动态库 + 测试
cmake -B build -S . \
    -DFLVPARSER_BUILD_TEST=ON \
    -DBUILD_SHARED_LIBS=ON \
    -DSPSC_INCLUDE_DIR=/path/to/spsc
cmake --build build --config Release
```

## 使用方法

### 基本流程

```cpp
#include "FlvParser.h"

FLVParser parser;

// 1. 喂入原始 FLV 字节流（可从网络/文件分批读取）
parser.parse(data, size);

// 2. 获取音视频序列头（首次解析成功后可用）
auto videoSeqHeader = parser.getVideoSeqHeader();
auto audioSeqHeader = parser.getAudioSeqHeader();

// 3. 逐帧取出解析结果
while (auto pkt = parser.getPacket()) {
    // pkt->pts      : 时间戳 (ms)
    // pkt->data     : NAL 数据（不含 FLV 封装）
    // pkt->size     : 数据长度
    // pkt->index    : 8=音频 / 9=视频 / 18=脚本
    // pkt->codec_id : 7=H.264 / 12=H.265 / 10=AAC
    // pkt->key_frame: 是否为关键帧
}

// 4. 停止解析（唤醒阻塞的生产线程）
parser.quit();
```

### Packet 结构

| 字段 | 类型 | 说明 |
|---|---|---|
| `pts` | `int64_t` | 时间戳（毫秒） |
| `data` | `uint8_t*` | 裸 NAL 数据指针（所有权归 Packet，析构时自动释放） |
| `size` | `uint32_t` | 数据长度 |
| `index` | `int32_t` | 多媒体类型：8=AUDIO, 9=VIDEO, 18=SCRIPT, 19=APPLICATION |
| `codec_id` | `int32_t` | 编码类型：7=AVC, 12=HEVC, 10=AAC |
| `type` | `uint8_t` | 包类型：0=Header, 1=Body |
| `key_frame` | `bool` | 是否为关键帧（仅视频有效） |
| `track_id` | `uint64_t` | 轨道 ID（需 `setHasTrack(true)` 启用） |
| `has_track_id` | `bool` | 是否携带有效 track_id |

### Track ID 关联

当 FLV 流中携带 Application Tag (type=19) 用于描述多路视频轨道时：

```cpp
parser.setHasTrack(true);  // 启用 track 关联
// 解析时自动将 Application Tag 的 track_id 绑定到同 PTS 的下一帧视频
```

## 内部架构

```
字节流输入
    │
    ▼
┌─────────────────┐
│  CircularBuffer  │  环形缓冲区，支持追加/消费
└────────┬────────┘
         ▼
┌─────────────────┐
│    FlvHeader     │  解析 FLV 文件头（9 字节），判断音/视频存在
└────────┬────────┘
         ▼
┌─────────────────┐
│     FlvTag       │  解析 Tag Header（类型/大小/时间戳/StreamID）
└────────┬────────┘
         ▼
┌──────────────────────────────────┐
│   FlvBaseTagDataFactory          │  根据 Tag 类型分发
│   ├── FlvVideoTagData           │  解析 AVC/HEVC NALU
│   ├── FlvAudioTagData           │  解析 AAC Raw
│   ├── FlvScriptTagData          │  脚本数据（metadata）
│   └── FlvApplicationTagData     │  Application 扩展数据
└────────┬─────────────────────────┘
         ▼
┌──────────────────────────────────┐
│  moodycamel::BlockingReaderWriter │  SPSC 有界队列（容量 128）
│  CircularBuffer<Packet*>          │  生产者: parse / 消费者: getPacket
└──────────────────────────────────┘
```

## 注意事项

- `parse()` 返回 `false` 表示数据不足以解析当前 Tag，需继续喂入更多数据
- `getPacket()` 非阻塞：无数据时返回 `nullptr`
- SPSC 队列满（128 帧）时 `parse()` 内部以 50ms 超时轮询等待消费，调用 `quit()` 可立即退出
- `Packet::data` 内存由 Packet 自身管理（`new[]`/`delete[]`），使用 `unique_ptr` 获取时自动释放
- `CircularBuffer` 初始容量 4096 字节，按需动态扩容（翻倍），无硬性上限
