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

## 本分支验证证据

| 命令/环境 | 结果 | 覆盖与边界 |
|---|---|---|
| `bash scripts/check-host.sh host`，GCC9.4/C++17/Release | 4/4 通过 | session、operations、video、concurrency；测试在 NDEBUG 下仍有效 |
| `cmake --build --preset host-asan` + 宿主 `ctest --preset host-asan` | 4/4 通过 | ASan + UBSan，默认泄漏检查开启，无 sanitizer 报告 |
| 编辑器 Problems | 无诊断 | 不替代编译与运行测试 |

测试证据：`build/host/Testing/Temporary/LastTest.log` 和
`build/host-asan/Testing/Temporary/LastTest.log`（构建产物不提交仓库）。
沙箱内 LeakSanitizer 曾因 ptrace 限制退出；宿主重跑通过，没有禁用泄漏检查。
提供 host-tsan preset，但尚未运行 TSan，不声称已通过数据竞争检测。

具体用例：3375 条三事件状态序列；1000 个操作的恰好一次终态；20000 帧并发提交/关闭；
4 线程关麦票据竞争；8 线程预算争用；重复 compositor release、拒绝提交和析构重入。
这仍是基础库测试，不是端到端采集关麦延迟、真实 DMA-BUF 安全或网络故障验收。

## 当前尚未交付

- 新客户端可运行的信令/媒体/Qt 应用；外部 MPP 解码 + Wayland 真实视频闭环。
- 正式鉴权/WSS/TURN 部署验收、真实声卡/AEC、长期稳定性、生产容量证明。
- GitHub 远端 `rtc-rk-win` 分支发布（本地分支不等于已推送）。
- MPP 池 generation 与分辨率切换的双池过渡：当前 Budget 不提供热替换事务，
  超出预算会安全拒绝；A3 需实现并验收，不能直接释放仍在显示的旧池。

## 紧接着的实施顺序

1. A2：版本锁定的 log-cpp、协议 codec、双向 RPC、WS/WSS 与 SFU 音频，CLI 可采证。
2. A3：MPP 外部工厂/NativeFrame/WebRTC 调度/Wayland 闭环，优先解决真实混合视频协商。
3. A4：Qt Store/Presenter/视频宿主接入同一 engine，交付首个可用应用包。
4. A5–A7：PTT/主持/呼叫、恢复/容量、生产安全与运维准入。

未遇到需要用户帮助的私有库接口/编译问题；后续若出现，按 AGENTS 先提供事实并提问。

旧 RK 工程曾通过的音频/日志/Qt 测试不能作为本分支测试证据；旧工程仍有混合视频协商崩溃，
本计划将以真实视频接收实现解决，而不是把视频隐藏掉。
