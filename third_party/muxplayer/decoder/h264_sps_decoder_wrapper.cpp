// ── h264_decode_sps 编译包装 ──
// h264_spsDecoder.h 包含全局函数和命名空间，直接 include 到 hw_decoder.cpp 会造成污染。
// 在独立 .cpp 文件中包含它，提供 h264_decode_sps 的链接实现。
#include "../demuxing/h264_spsDecoder.h"
