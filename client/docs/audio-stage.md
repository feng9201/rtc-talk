# A2 SFU 音频阶段：代码、边界与验证入口

状态：代码已接入；宿主与板端测试通过，完整 CLI 的合成音频双向 SFU 短测通过。
真实声卡/AEC、恢复和公共 API 尚未验收，不代表完整 A2 或生产应用完成。
这不是完整应用交付，也不是生产验收。

## 实现边界

- [Audio port](../include/rtc_client/media/audio.h)：音频配置、SDP 发布/应答、静音、设备、统计。
- [AudioWorker](../libs/media/worker.cpp)：单独执行 SDK 调用，默认最多 8 个在途/排队任务；
  满时拒绝，关闭时立即关 gate、取消尚未执行任务、在所属线程销毁后端。结果回到控制执行器。
- [M146 后端](../libs/media/webrtc_audio.cpp)：独立 send/recv PC、Opus 32kbps、
  ALSA/Pulse ADM、实际声卡启用 AEC/NS/AGC2；采集编码前通过 lock-free MicrophoneGate 关麦。
  设备必须按 ADM 枚举的 ID 显式选定，不任意使用默认设备。硬件映射/AEC 效果待板测。
- [CLI](../apps/rtc-cli/main.cpp)：hello 真实声明 Opus/P2P=false/video.max_recv=0，
  音频发布后默认保持静音；显式 `--unmute` 才申请服务端授权，成功并确认关麦票据仍有效后开启 gate。
  media.forceMute/floor.revoked/policyChanged、退房、连接关闭、退出均先关 gate。
- 收到视频 offer 明确返回 488，绝不修改 SDP，也不宣称已完成视频接收。
  A3 必须接入真正 MPP factory/NativeFrame/Wayland 后再声明视频能力。

当前单进程退出会等待 SDK 安全回收。SDP/stats future 各有 8s 截止时间；SDK 内部
BlockingCall/驱动若永久卡住，当前没有强杀线程的安全机制，生产需进程 watchdog 和故障验收。
重连/resume、TURN TTL 刷新/ICE restart、C ABI、floor/host/call、Qt 尚未集成，不能当作完整 A2。

## 构建

SDK 根通过环境变量 `WEBRTC_SOURCE_DIR`、`WEBRTC_BUILD_DIR` 导入，SDK 内的 args.gn 检查
ARM64、非 LTO、libstdc++ ABI、CREL/H265 设置；业务模块 C++17，SDK 翻译单元 C++20。
vcpkg 依赖安装在本产品 build 下；不链接旧 RK 或 sbdc 的 build 产物。
最终链接保留旧 RK 已使用的 `--gc-sections`，移除当前可执行文件不引用的 SDK 节。
本次仅增加这一链接选项后，原 `__arm_tpidr2_save` 错误消失；未修改 SDK、补空实现或放宽未定义符号检查。
readelf 确认两个可执行文件中均未保留该符号及两个报错的 SME 旋转函数。
这仅证明当前音频目标可链接，不能推断后续所有视频转换路径均具备运行时兼容性。

```bash
bash scripts/build-rk-signaling.sh
bash scripts/build-rk-audio.sh                  # 默认不编入合成设备
bash scripts/build-rk-audio.sh rk3576-audio-dev # 显式合成设备，仅开发
```

真实声卡 CLI 入口（Token 通过 stdin，不写参数/文档/仓库）：

```text
rtc-cli --audio --token-stdin --url wss://host/talk/v1/ws --room ROOM
        --audio-backend alsa --recording-device ID --playout-device ID
        [--unmute] [--duration 1..60] [--ca-file FILE]
```

## 验证分层

1. 宿主 `ctest --preset host-net`：包含 worker 满队列、停止取消、过期关麦票据、错误码传播。
2. 开发包 `rtc_audio_backend_test`：实际 M146 构造/销毁、Opus SDP、无视频上行、默认关麦、
   视频 offer 拒绝；必须在 RK 运行，不能用宿主测试替代。
3. [板端采证脚本](../scripts/board-audio-smoke.py)：独立 `/tmp` 目录，双实例随机房间，
   同时检查 RTP 发送/接收包数、播放端非零信号、约 440Hz 解码结果；另测默认静音实例无上行 RTP。
   不操作已有房间，不修改服务、系统库、混音器或显示。密码来自环境变量，Token 仅 stdin。
4. 后续真实设备、Windows 互通、抖动/丢包/撤权/AEC 和视频混合房间另行验收。

第 2 项及新增两套 SSL 共存、真实回环 WSS/证书拒绝反例通过，共 12/12。
第 3 项最终报告通过：A 发送 502/接收 495 包，B 发送 501/接收 494 包，
两端均检测到 987 帧非零信号、440Hz；默认静音实例发送 0 包；均正常退房并 exit=0、无 stderr。
三实例均确认媒体模块日志实际落盘。合成音频不证明声卡路由、扬声器或 AEC 正确。
目录 `/tmp/rtc-rk-audio.7ATKuwyC`，报告 `build/rk3576-audio-dev/board-audio-smoke.json`。
历史入房后 255 已借远端退出标记确认实际为 SIGSEGV=139，core 确认 BoringSSL 调到系统 OpenSSL；
修复为独立媒体动态库和符号隔离，见 [ADR 001](adr/001-isolate-media-tls.md)。
部署脚本已兼容本机 OpenSSH8.2（无 `-O`）与提供该参数的新版本；不改变板端 SSH 配置。
脚本保留独立 `/tmp` 目录供诊断，不自动清理用户板端文件；默认禁用测试进程 core，
仅显式单实例诊断可在该目录生成受限 core，不修改系统 core_pattern。
