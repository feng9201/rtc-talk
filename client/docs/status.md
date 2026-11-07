# 实现与验证状态

更新：2026-11-07。

## 已完成

- 新 worktree：`rtc-rk/rtc-rk-win`，新本地分支 `rtc-rk-win`，基于服务端 `origin/main@8e6ff57`。
- 旧 `rtc-rk/rtc-talk` 的未提交实现保持原位，未切分支或覆盖。
- 生产架构、目录、API、UI 和阶段验收文档已落盘。
- 已读取 sbdc 当前 src/render/wayland 的关键接口与实现；不采用废弃 muxDecoder。
- A1 基础模块已提供源码，不含模拟“成功”的产品入口：
  - SessionLifecycle：显式状态、generation/revision、过期异步结果拒绝。
  - MicrophoneGate：lock-free 安全关麦票据，迟到的开麦授权不能越过后续关麦。
  - Operations/RecoveryWindow：有界 pending、截止时间、恰好一个终态结果、有界退避/恢复窗口。
  - DecodedFrame：NV12 fd/stride/offset/size/rotation 校验、opaque owner 保活。
  - LatestFrameMailbox / InFlightFrames：pending 与 committed 分离、锁外释放、在途限额。
  - VideoBudget：分辨率/fps/像素吞吐/帧池内存的 RAII 准入，拒绝不改变用量。
- A2a 信令与日志已实现：
  - talk.v1 封装校验：256KiB、32 层、数组/对象/ID/方法及字段类型限制。
  - 双向 RPC：请求命名空间分离、有界 pending/replay、deadline、取消/旧代响应拒绝。
  - WS/WSS：默认拒绝明文、TLS>=1.2、CA/主机名验证、实际子协议响应头校验、连接/关闭截止时间。
  - log-cpp：ABI v1 动态库 SDK，私有源码不提交；白名单结构化字段、有界异步/轮转。
    源码 pin `d8062b7a74c86028a3a927e76e590f986ce7267b`；已生成 x86_64/ARM64 .so、独立 C 头和 SHA256 manifest。
  - 实际信令 CLI：hello/heartbeat/join/leave/bye，仅接受 stdin 凭证、8s 输入截止时间。
  - 真实服务器确认并修复：最后成员退房先收到 room.closed(empty) 不能被误判为 403。
- A2 音频代码已接入（板端合成音频双向闭环通过，真实设备待验收）：独立有界媒体 worker、M146 SFU send/recv PC、
  Opus、ADM/APM、采集 gate、显式服务端授权开麦、强制静音/退出保护、周期 RTP/播放统计。
  合成设备仅显式开发 preset 编入；实际设备要求指定 ID。见 [音频阶段](audio-stage.md)。

## 本分支验证证据

| 命令/环境 | 结果 | 覆盖与边界 |
|---|---|---|
| `bash scripts/check-host.sh host`，GCC9.4/C++17/Release | 4/4 通过 | session、operations、video、concurrency；测试在 NDEBUG 下仍有效 |
| `cmake --build --preset host-asan` + 宿主 `ctest --preset host-asan` | 4/4 通过 | ASan + UBSan，默认泄漏检查开启，无 sanitizer 报告 |
| 编辑器 Problems | 无诊断 | 不替代编译与运行测试 |
| `ctest --preset host-net` | 11/11 通过 | 新增纯 C 日志 ABI、媒体 worker；含真实回环 WS/WSS、证书拒绝反例、CLI 心跳和退房通知顺序 |
| `cmake --build --preset host-net-asan` + `ctest --preset host-net-asan`，宿主执行 | 11/11 通过 | 自有 foundation/protocol/logging/signaling/media worker/CLI 插桩；默认泄漏检查开启 |
| `signaling-smoke.py`，开发服务器独立随机空房间 | 通过，exit=0 | hello→join→leave→bye→close；无错误；未操作 rk-client |
| `build-rk-audio.sh rk3576-audio-dev`，同一 SDK / GCC12.4 | 通过 | 仅增加 `--gc-sections` 后 CLI 与后端测试成功链接，无空符号/SDK 修改 |
| `build-rk-audio.sh rk3576-audio` + 本地 CMake install | 通过 | RTC_ENABLE_TEST_AUDIO=OFF；CLI RPATH 为 $ORIGIN/../lib，媒体库为 $ORIGIN；不等同于实际声卡验收 |
| 板端基础/日志/协议/媒体 worker/真实 M146 后端/SSL 共存/WSS | 12/12 通过 | `/tmp/rtc-rk-audio.7ATKuwyC`；含 Opus SDP、关麦、视频拒绝、SSL context 共存、WSS 证书正反例；不含真实声卡/MPP |
| 板端两个开麦实例 + 单独默认静音实例 | 通过 | 双向 RTP 502/495 和 501/494；两端 987 信号帧、440Hz；默认静音上行 0；exit=0，无 stderr；三实例媒体日志均落盘 |

新增证据：`build/host-net/Testing/Temporary/LastTest.log`、
`build/host-net-asan/Testing/Temporary/LastTest.log`、`build/host-net/signaling-smoke.json`、
`build/rk3576-audio-dev/board-audio-smoke.json`。
ASan 不代表未插桩的预编译依赖也经过了内存检查。11 项回归包含新的二进制日志 SDK 消费路径。
宿主平台 TLS 为 OpenSSL 1.1.1f，RK SDK 头文件为 3.2.1；仅记录检测事实，安全补丁/发行准入尚未核查。

测试证据：`build/host/Testing/Temporary/LastTest.log` 和
`build/host-asan/Testing/Temporary/LastTest.log`（构建产物不提交仓库）。
沙箱内 LeakSanitizer 曾因 ptrace 限制退出；宿主重跑通过，没有禁用泄漏检查。
提供 host-tsan preset，但尚未运行 TSan，不声称已通过数据竞争检测。

具体用例：3375 条三事件状态序列；1000 个操作的恰好一次终态；20000 帧并发提交/关闭；
4 线程关麦票据竞争；8 线程预算争用；重复 compositor release、拒绝提交和析构重入。
这仍是基础库测试，不是端到端采集关麦延迟、真实 DMA-BUF 安全或网络故障验收。

## 当前尚未交付

- 新客户端真实音视频和 Qt 应用；外部 MPP 解码 + Wayland 真实视频闭环。
- A2 尚未完成：SFU Opus 双向合成音频短测通过，但真实声卡/AEC、
  生产会话控制器/C ABI、完整恢复路径尚未验收或接入。
- 正式鉴权/WSS/TURN 部署验收、真实声卡/AEC、长期稳定性、生产容量证明。
- MPP 池 generation 与分辨率切换的双池过渡：当前 Budget 不提供热替换事务，
  超出预算会安全拒绝；A3 需实现并验收，不能直接释放仍在显示的旧池。

## 紧接着的实施顺序

1. A2：真实声卡映射/采播/AEC 验证，补齐恢复与 API；已有双向合成音频闭环可供回归。
2. A3：MPP 外部工厂/NativeFrame/WebRTC 调度/Wayland 闭环，优先解决真实混合视频协商。
3. A4：Qt Store/Presenter/视频宿主接入同一 engine，交付首个可用应用包。
4. A5–A7：PTT/主持/呼叫、恢复/容量、生产安全与运维准入。

## 依赖处理及链接问题验证结论

- log-cpp 源码获取阻塞已由用户提供本地源码解除。SDK 已成功构建两种架构，应用消费动态库和独立头文件；
  `thirdPart/log-cpp` 已精确忽略，未提交私有源码。公共 fmt/spdlog 的许可证随 SDK 保存。
- 新 RK 信令构建产物已生成；没有新 log-cpp 编译错误。
- 原最终链接 `rtc-cli` / `rtc_audio_backend_test` 失败：
  `libwebrtc.a(rotate_sme.o): undefined reference to __arm_tpidr2_save`，涉及
  `TransposeWxH_SME` / `TransposeUVWxH_SME`。
- SDK：`/home/wdz/projects/hxf/webrtc_m146/src/out/rk3576_release/obj/libwebrtc.a`，工具链 RK Buildroot GCC12.4。
  对齐旧工程的 `--gc-sections`，仅此一项链接变更即解决当前两个目标的链接错误。
  未补空实现、未放宽 undefined、未修改 SDK；readelf 未发现两个目标保留该符号/两个 SME 旋转函数。
- 结论：本次音频目标的阻塞来自新工程遗漏链接裁剪配置，不能据此判断 libwebrtc 功能不足或要求更换 SDK。
  后续视频路径须另做实际链接/运行验证，不能扩大此结论。
- 入房后 255 已定位为真实 SIGSEGV=139；core 显示 BoringSSL 证书生成调用到系统 OpenSSL3 ASN1_item_new。
  已通过独立媒体 DSO/符号隔离修复，自动验证无系统 libssl/libcrypto 依赖及无额外导出，见 [ADR 001](adr/001-isolate-media-tls.md)。
- 最新板端 12/12、双向合成音频、默认静音、正常退出和媒体日志均通过。
  测试未操作现有 rk-client 房间，未修改声卡、显示、系统库或服务；未修改 SDK。

## Git 交付

产品分支：`rtc-rk-win`；源码目录：`client/`。
本地分支和提交已建立，工作区位于独立目录，旧工作区修改保持原样。
用户已执行 push，当前本地 `rtc-rk-win` 正确跟踪 `origin/rtc-rk-win`，此前分支发布阻塞已解除。
普通客户端构建已不依赖 log-cpp 远端源码认证；后续源码 SDK 重建使用用户提供的固定本地源码。

旧 RK 工程曾通过的音频/日志/Qt 测试不能作为本分支测试证据；旧工程仍有混合视频协商崩溃，
本计划将以真实视频接收实现解决，而不是把视频隐藏掉。
