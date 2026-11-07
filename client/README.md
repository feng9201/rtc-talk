# RK 音频上行 / 音视频下行客户端

目标：可维护、可扩展、具备生产准入门槛的 RK3576 原生对讲客户端。
RK 不包含摄像头采集、屏幕采集和视频上行，客户端不依赖 Node。
分支名称 `rtc-rk-win` 不代表本轮重写 Windows 客户端；Windows 是互通参考端。

## 先读设计

1. [系统架构](docs/architecture.md)：系统边界、媒体链路、线程与所有权、安全。
2. [目录及依赖](docs/directory.md)：模块落点、构建隔离与依赖锁定。
3. [API 契约](docs/api.md)：现有服务端端点、本地 API、错误及事件语义。
4. [UI 架构](docs/ui.md)：页面、状态模型、渲染宿主及交互。
5. [生产开发计划](docs/development-plan.md)：里程碑、交付物、验收与风险。
6. [实现和验证状态](docs/status.md)：唯一的实际完成情况记录。

本仓库根目录保留服务端基线，不意味着板端部署或运行服务端。
本目录是新的客户端产品根；旧 RK 工作区的未提交代码未被移动或覆盖。
设计覆盖完整产品，代码按里程碑交付；**架构完成不等于应用可投产**。

## 当前可构建代码

当前提供不依赖第三方库的内核：会话状态机、关麦安全 gate、操作终态登记/恢复退避、
NV12 帧布局校验、解码资源预算、latest-frame mailbox 和 compositor 在途帧租约。
已新增实际 WS/WSS、双向 RPC、二进制日志 SDK；SFU Opus 音频后端和 CLI 编排代码
已经接入，板端合成音频双向 SFU 短测、默认静音与日志落盘验证通过。
真实声卡/AEC、完整恢复及产品 API 尚未验收，不能称为完整 A2 或生产应用。
硬解、Wayland 和 UI 尚未接入。

在本目录运行：

```bash
bash scripts/check-host.sh host
bash scripts/check-host.sh host-asan
```

测试包含并发和析构重入场景。LeakSanitizer 必须在允许其检查进程的环境运行，
不得通过关闭泄漏检查把沙箱失败伪装为通过。

## A2a：信令与日志

设置 `VCPKG_ROOT` 为开发机已批准的 vcpkg 目录后：

```bash
bash scripts/install-host-deps.sh
bash scripts/check-net.sh host-net
bash scripts/check-net.sh host-net-asan
```

依赖安装/构建/下载缓存全部位于本目录 `build/`，不引用旧 RK 或 sbdc 的构建产物。
OpenSSL 使用平台 SDK/系统的开发库；实际检测版本记录在 CMake 输出。当前版本及安全补丁
尚未完成生产安全审计，TLS 正反例通过不代表系统库已获准生产发行。

`build/host-net/rtc-cli --help` 查看诊断入口。当前 CLI 必须显式指定
`--signaling-only --token-stdin`：不发布音视频，不虚报 codec 或 P2P 能力；
若收到媒体协商请求会明确返回不支持，并以退出码 3 区分“信令成功但媒体未启用”。
退出码 2 表示配置、连接或协议错误；0 只表示完成了本次信令诊断，不代表媒体可用。

独立临时空房间短测（仅开发服务器）：

```bash
python3 scripts/signaling-smoke.py --server http://47.117.159.193:8080 \
  --cli build/host-net/rtc-cli --dev-token --report build/host-net/signaling-smoke.json
```

脚本自行生成随机 uid/房间，Token 只通过 stdin 传给 CLI；报告不含 JWT。
不应使用这条诊断链路替代真实媒体验证，也不要将 `/dev/token` 用于生产登录。
Python 是开发测试工具的依赖，不是客户端二进制运行依赖；客户端不需要 Node。

### 日志

默认 `/tmp/rtc-rk-client/signaling-<pid>.log`，可用 `--log-file` 设置本进程路径。
应用使用 [日志 SDK](third_party/log-cpp/README.md) 的独立 C 头文件和动态库；
私有 log-cpp 源码不入库。SDK 固定源码提交 `d8062b7a74c86028a3a927e76e590f986ce7267b`，
fmt/spdlog 静态封装在动态库内，不向应用暴露它们的头文件或 C++ ABI。
文件 2MiB ×（当前 + 4 轮转），异步队列 2048、满时丢旧记录。
记录会话 generation、请求方法、操作序号、结果码和耗时；仅允许白名单标量字段，
不记录认证头、Token、SDP、ICE 密钥、原始请求正文、任意异常文本。
多个 Runtime 共享首个日志配置，最后一个释放时 drain；实时音频回调不得调用日志。

### RK 信令构建

设置 `RK3576_HOST` 为经批准的 Buildroot host 目录后：

```bash
bash scripts/build-rk-signaling.sh
```

日志私有源下载问题已通过用户提供源码解决；普通客户端构建不再下载该私有库。
日志 SDK 已生成 Linux x86_64/ARM64 制品，配置时检查 ABI 和 SHA256。

### RK 音频实现与验证入口

参见 [音频阶段说明](docs/audio-stage.md)。设置 `WEBRTC_SOURCE_DIR` 和
`WEBRTC_BUILD_DIR` 指向匹配的 M146 SDK 后：

```bash
bash scripts/build-rk-audio.sh                  # 实际声卡版本，无合成设备
bash scripts/build-rk-audio.sh rk3576-audio-dev # 显式开发版本，包含合成设备
```

目前代码支持音频发送/接收、默认静音、显式授权开麦、有界独立媒体队列和 RTP/播放统计。
最终链接问题已通过对齐旧工程的 `--gc-sections` 解决，未修改 SDK 或补空符号。
入房后崩溃已定位并修复：媒体/BoringSSL 独立动态库隔离于信令 OpenSSL，详见
[ADR 001](docs/adr/001-isolate-media-tls.md)。板端 12/12 通过（含两套 SSL 共存及 WSS 正反例），
两个实例双向 Opus 收发和约 440Hz 播放信号通过，默认静音上行 0 包，见 [状态](docs/status.md)。
视频 offer 暂明确返回 488；不会删去 video m-line 假装协商成功。混合房间需等待 A3。

vcpkg 参考 Windows 工程的环境变量、manifest/baseline、triplet 和独立安装目录策略；
RK 保留已经验证的 baseline，不照搬 Windows 专用 triplet/FFmpeg 或更换依赖版本。
