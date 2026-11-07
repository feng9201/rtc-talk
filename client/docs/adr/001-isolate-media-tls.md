# ADR 001：隔离 WebRTC/BoringSSL 与信令 OpenSSL

状态：已实施，板端共存及音频短测通过。日期：2026-11-07。

## 证据与原因

旧的静态媒体链接使系统 OpenSSL 3 先于 libwebrtc.a 满足部分同名引用。
CLI 入房后在生成 DTLS 证书时发生 SIGSEGV；受限测试 core 的无参数栈为：

```text
libcrypto.so.3: asn1_item_embed_new
libcrypto.so.3: ASN1_item_new
webrtc::(anonymous namespace)::AddTime
webrtc::BoringSSLCertificate::Generate
webrtc::BoringSSLIdentity::CreateInternal
webrtc::RTCCertificateGenerator::GenerateCertificate
```

因此是同进程混用两套不兼容的 SSL 实现，不是 SSH 故障、音频编码器不足，
也不应通过关闭 WSS 验证、更换 SDP 或补空函数解决。

## 决策

- [rtc_audio](../../CMakeLists.txt) 为独立共享库，私有链接 SDK 静态归档。
- hidden visibility + [导出表](../../libs/media/exports.map) + exclude-libs + Bsymbolic，
  不将 BoringSSL/WebRTC/STL 实现作为可见接口泄漏给主程序。
- 媒体库保持 `-z defs`，禁止未定义符号放行；构建后自动检查无系统 libssl/libcrypto 依赖和无多余导出。
- 主程序的 WS/WSS 仍使用系统 OpenSSL，保持证书链、主机名/IP、过期证书拒绝与 TLS>=1.2。
- Audio factory/port 为同次构建、同工具链的**内部 C++ 接口**，不是对外承诺的稳定 SDK ABI。
  公开产品 C ABI 仍按架构单独实现，不向调用方暴露 STL/WebRTC。
- 日志由共享 C SDK 持有唯一状态；主程序管理 Runtime 生命周期，隔离模块的 formatter 可直接安全写入 SDK。
  模块不各自初始化或销毁日志单例；关停仍先停所有 SDK/媒体线程，最后 flush。
- 未修改用户提供的 WebRTC、log-cpp 源码或系统库。

## 验证与边界

- ELF 隔离正例通过；将依赖 OpenSSL 的主程序交给检查器时按预期拒绝（负例）。
- 板端音频后端、OpenSSL context + WebRTC 证书共存、真实回环 WSS/证书拒绝反例均通过。
- 独立随机房间双实例 Opus + SRTP 转发/解码检测通过；默认静音上行 0 包；媒体日志确实落盘。
- 尚未证明真实声卡/AEC、MPP/Wayland 视频或生产部署验收完成，详见 [状态](../status.md)。
- core 仅为本次显式诊断生成，可能含短期测试凭证；仅保留在受限临时目录和 ignored build，禁止入库或附入报告。
