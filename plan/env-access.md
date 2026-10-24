# 环境、账号与进度速查

> 给接手人或换机器时用。口令只写在测试机用途上，**不要**把测试机密码同步到正式环境。
> 更新：2026-10-24（`fd84db5` 已发布：mid 归一化修复两路视频 409；**RK 端暂停，优先调通 Windows 端**；Windows 客户端视频一期完成，P2P 支持摄像头/屏幕共享）

## 1. 当前进度

| 阶段 | 状态 | 说明 |
|---|---|---|
| **S0 环境** | ✅ 完成 | 开发容器、coturn、测试机初始化、打包/发布/回滚、TURN 验证、`/healthz` 公网可达 |
| **S1 信令骨架** | ✅ 完成 | WS 信令、JWT、房间进出与成员通知、断线恢复、顶号；38 个用例；bot 公网验收通过 |
| **S2 SFU 语音 MVP** | ✅ 完成 | Router + WebRtcServer、标准 SDP 发布/订阅、开关麦 pause/resume；werift bot 互通 Opus |
| **S3 P2P 与模式切换** | ✅ 服务端完成 · 客户端 C5 代码完成 | 服务端 `36a5d09`：`call.*`、`p2p.*` 转发、`room.mediaMode` 先建后断、TURN 临时凭证；RK 客户端 `8c7e12b`：P2P Peer、模式切换、呼叫与 UI；公网信令联调（无媒体）通过，板端合成音频 P2P 待测（**RK 线 10-24 起暂停，见下**） |
| **视频 MVP + 10-23/24 修复** | ✅ 已发布 `fd84db5` | 单层 H.264（`62df51b`）；toffset 白名单过滤（`5e2d161`，固化测试机热补丁）；房间视频策略生效（`79d5223`：`enabled` 默认 true、`max_publishers` 按 producer 计数、同 uid 同 source 去重，违反 409）；`sfu.publish` 重试未应答的 recv offer（`0f62af9`）；bot 先注册 offer handler 再发布（`bf1c985`）；SDP mid 归一化 string（`fd84db5`：修复两路视频被判 duplicate camera 的 409、screen producer 自 `62df51b` 起被误标 camera）。59 用例通过，3×3 bot 公网音频冒烟通过 |
| **Windows 客户端视频一期** | ✅ 代码完成 · 已联调 | `client-win`（`feat/windows-client`）：SFU 多人视频与一人两路（摄像头+屏幕）；P2P 预发布 audio/camera/screen 三条 m-line，第三人入房自动切 SFU 续传；P2P 下视频启停与 UI 按钮放开。`talk-cli` 公网双端联调：P2P 摄像头/屏幕 720p15 双向收发、三切 SFU 无断流、P2P 下 `video.stop/start` 正常；服务端零改动 |
| S4 对讲形态（PTT/host） | 未开始 | — |
| S5 稳定性 | 未开始 | — |

**已发布到测试机**：`fd84db5`（S3 + H.264 视频 + 10-23/24 全部修复）；客户端：Windows `feat/windows-client` 联调中、RK `feat/rk3576-client` `8c7e12b`（暂停）。  
**健康检查**：`http://47.117.159.193:8080/healthz`  
**信令**：`ws://47.117.159.193:8080/talk/v1/ws`（子协议 `talk.v1.json`）  
**开发 token**：`GET http://47.117.159.193:8080/dev/token?uid=xxx&rooms=room1`（仅 `TALK_ENV=dev`）

### 下一步（2026-10-24 调整：优先 Windows，RK 暂停）要做的事

1. **Windows 客户端对 `fd84db5` 实测**：两路视频（摄像头 + 屏幕共享）发布/订阅（修复前是 409 duplicate camera）；`media.setMute{camera}` 只停摄像头不停共享；3~4 人视频房宫格与 mid 分配；真人听音（回声 / 双讲 / 系统音采集源）。已完成（`talk-cli`）：P2P 摄像头/屏幕 720p15 双向收发、第三人入房切 SFU 续传、P2P 下视频启停；剩余 Qt 界面端到端验收（本地/远端画面、按钮联动、P2P↔SFU 切换后的画面归属）。
2. 跨 NAT / 强制中继场景与 P2P→SFU 断音时长实测（Windows 端做）。
3. S4 对讲形态（PTT/host）与 S5 稳定性（resume、ICE restart、弱网 tc netem、24h 长稳）。
4. 视频 P1（背景与方案见 wiki.md §8/§10）：producer pause/resume 状态广播（目前接收端只能靠 5 秒无帧超时回收画面）；同 mid producer 复用（ICE restart 重发布会连带重建全部订阅端 consumer，全员闪断）。
5. **RK3576 客户端暂停**：板端 C5 验证、双端媒体联调全部后置，等 Windows 端调通、协议与服务端稳定后再恢复；恢复入口见 [RK3576 客户端开发计划](./RK3576_客户端_开发计划_b524300f.md)。

详细阶段表见 [rtc-server-dev-plan.md §4.6](./rtc-server-dev-plan.md)。

公网音频验收：

```bash
npm run bot -- --url http://47.117.159.193:8080 --count 4 --duration 3 --audio
```

---

## 2. GitHub 仓库

| 项 | 值 |
|---|---|
| 仓库 | https://github.com/feng9201/rtc-talk |
| SSH remote | `git@github.com:feng9201/rtc-talk.git` |
| 默认分支 | `main` |
| GitHub 账号 | `feng9201`（登录邮箱 `hxfengv@163.com`） |
| 本地提交身份 | `hxfeng <hxfengv@163.com>`（用 `git -c user.name=... -c user.email=...`，不改全局 git config） |
| 推送方式 | 开发机上配置了 GitHub deploy key / SSH，可 `git push origin main` |
| 本地目录 | `/home/wdz/projects/rtc-talk` |

提交时系统自带 git 2.25 太旧，用：

```bash
export PATH=/home/wdz/projects/hxf/webrtc_m146/tools/git/bin:$PATH   # git 2.56
```

---

## 3. 开发机

| 项 | 值 |
|---|---|
| 地址 | `172.16.21.21`（公司内网） |
| 主机名 | `wdz-virtual-machine` |
| 系统 | Ubuntu 20.04，内核 5.15，glibc 2.31 |
| 登录用户 | `wdz`（Cursor Remote-SSH 连此用户） |
| 共用注意 | 与 `huangzuhong` 共用 Docker；资源用 `wdz-` 前缀（compose 项目 `wdz-talk`、构建器 `wdz-builder`） |
| Docker | 20.10.9；**不要升级**；开发容器 `seccomp=unconfined` |
| 开发容器 | `deploy/docker/dev.sh up|shell|exec`；镜像 `rtc-talk-dev:node22`（Ubuntu 24.04 + Node 22.23.3） |
| 本机 coturn | 容器 `wdz-coturn`，`172.16.21.21:13478`，中继 49300–49340，开发密钥 `talk-dev-turn-secret` |
| SSH 公钥（已加到测试机 talk） | 见下方「公钥」 |

**开发机 → 测试机免密登录用的公钥**（`~/.ssh/id_rsa.pub`，comment `system_devel@ovopark.com`）：

```
ssh-rsa AAAAB3NzaC1yc2EAAAADAQABAAABgQDOTPIzTxpGK9UG4ABonOLzxp3OCHITpAN/7zkonigp4yzoUbby4nXtt8/kxylqrOa+eIiSuIceQ2JQ5s/cxo4JolxRBPbQvZPpHkYs9zvcO76Oex0szrvLCbfFJkmsBb2Ksuu556yszK5QzyQgA3JX9KVn0vioiVcW+aWE665Mu136Fbgpu+i/WV3rdLRHr4ZxuehTh15q83P7ctWUASN0DwhGMOTy4/FgAOdTGO5/pZ9m06/4MwIAkrwo6T5KvdMlC5nwqDXI1Po7U/iMznbaH+yoMfDVig8A+fmzD/4WdoIfShJR/TuAu1dR2cAZI6v2esFztEUfzE7a3nGkCbrz2IOFs8QwFVAB1WL9ets3UqvgMdT/3fv0tRtc5wzxwWXK9hsQLsjBYLokw/1X/yw8auNEFZGo1Q3DKbnpXDvSWb9wNBT+YbadstLilCf3yz49NyTJU6XP9EYu19dlNnFLemcSC/T6tX7BkkuEw2LZevwqOpeLHEgglBw1wV58vyM= system_devel@ovopark.com
```

---

## 4. 测试机（阿里云）

| 项 | 值 |
|---|---|
| 公网 IP | `47.117.159.193` |
| 内网 IP | `172.24.53.130` |
| 规格 | `ecs.e-c1m1.large`，2C / 1.6GB + 2GB swap，Ubuntu 24.04 |
| SSH | `ssh talk@47.117.159.193`（开发机公钥免密）；也可用密码登录 |
| 业务用户 | `talk`（在 sudo 组；口令见下） |
| root | 密码已改（不在此文档写 root 口令）；按决定仍允许密码登录，未做 `--harden-ssh` |
| Node | `/opt/node`，Node 22.23.3 |
| 服务 | systemd `talk-server`；coturn 独立服务 |
| 配置 | `/etc/talk/talk-server.env`（root:talk 640；含 `TALK_TURN_SECRET`、`TALK_JWT_SECRET`） |
| 发布目录 | `/opt/talk/releases/<时间>-<rev>`，软链 `current` / `previous` |
| 端口 | TCP 8080（HTTP/WS）；UDP+TCP 44444（mediasoup）；UDP/TCP 3478 + 中继 49160–49200（coturn） |

**talk 用户口令（测试机专用）**：`Talk5289.`  
sudo 示例：`echo 'Talk5289.' | sudo -S -p '' <命令>`；对 `talk-server` 启停已配置免密 sudo。

常用：

```bash
ssh talk@47.117.159.193
sudo systemctl status talk-server coturn
journalctl -u talk-server -f          # talk 在 systemd-journal 组，一般无需 sudo
curl -s http://127.0.0.1:8080/healthz
```

部署（在开发机仓库根目录）：

```bash
deploy/docker/dev.sh exec bash deploy/package.sh
deploy/deploy.sh release/talk-server-<rev>.tar.gz
deploy/deploy.sh --status | --rollback
```

公网 bot 验收：

```bash
npm run bot -- --url http://47.117.159.193:8080 --room s1-accept --count 4 --duration 30 --resume
```

---

## 5. 服务端运行参数（测试机）

写在 `/etc/talk/talk-server.env`（密钥不入库，只说明字段含义）：

| 变量 | 用途 |
|---|---|
| `TALK_ENV=dev` | 打开 `/dev/token` |
| `TALK_HTTP_PORT=8080` | 信令 HTTP/WS |
| `TALK_RTC_PORT=44444` | mediasoup WebRtcServer |
| `TALK_RTC_ANNOUNCED_ADDRESS=47.117.159.193` | ICE 公网地址 |
| `TALK_TURN_URLS` | turn:47.117.159.193:3478?... |
| `TALK_TURN_SECRET` | 与 coturn `use-auth-secret` 一致（脚本生成） |
| `TALK_JWT_SECRET` | JWT HS256（≥32 字符，脚本生成并持久化） |

重新生成/补齐密钥：在测试机以 root 再跑一次 `setup-test-server.sh`（已有密钥会 `existing()` 保留）。

---

## 6. 仍缺、需要你提供的

| 事项 | 何时需要 |
|---|---|
| 3~4 名测试同事 + Windows/耳机/4G | S2 后真人联调 |
| RK3576 板子 SSH、音频 codec、是否硬件回采 | 板子已可访问（见 §8）；真实声卡映射/硬件 AEC 待现场验证 |
| Windows 开发机与 `webrtc_sdk_hevc` 访问方式 | 编 `talk-cli` / S2 前尽量给 |

---

## 7. 文档索引

| 文档 | 内容 |
|---|---|
| [project-plan.md](./project-plan.md) | 需求、选型、整体架构 |
| [talk-protocol.md](./talk-protocol.md) | 信令协议 v1 |
| [rtc-server-dev-plan.md](./rtc-server-dev-plan.md) | 服务端开发计划与 S0/S1 执行记录 |
| [rk3576-libwebrtc-build-plan.md](./rk3576-libwebrtc-build-plan.md) | RK3576 libwebrtc 编译 |
| [RK3576_客户端_开发计划_b524300f.md](./RK3576_客户端_开发计划_b524300f.md) | RK3576 对讲客户端开发计划与实施进度（§9） |
| [diagnosis-sfu-publish-409.md](./diagnosis-sfu-publish-409.md) | 并发入房 409 诊断报告与修复验证（§8） |
| 本文 | 进度、下一步、机器账号、公钥、仓库 |

---

## 8. RK3576 对讲客户端（独立开发线）

> 与服务端 S 阶段并行的纯 C++/Qt5 客户端。计划与验收边界见 `RK3576_客户端_开发计划_b524300f.md` §9。

| 项 | 值 |
|---|---|
| 工作树 | `/home/wdz/projects/rtc-talk-rk3576`（git worktree） |
| 分支 | `feat/rk3576-client`，已推送 origin（`git@github.com:feng9201/rtc-talk.git`） |
| 里程碑提交 | `ad3d92d`（C0–C2：talk-core 信令+M146 音频域+Qt 界面+构建/打包/联调脚本）；`8c7e12b`（C5：呼叫、P2P 与 SFU/P2P 切换）；`257afa4`（P2P 路径显示） |
| 交叉工具链 | `/opt/rockchip/rk3576/host`（GCC 12.4、CMake、Qt5 tools、webrtc-checkout、5.4GB 只读 sysroot） |
| 开发板 | `root@172.16.123.11`，Buildroot，aarch64，Weston/Wayland，源码未 strip；板端口令沿用既有测试口令，未在此重复 |
| 板上运行 | `client/scripts/run-qt.sh` 设 Wayland/XDG/字体目录；`diagnose-board.sh` 只读采集音频/显示/输入；诊断走 stdout，不在板写日志 |

**当前进度**（详见计划 §9）：

- C0/C1/C2 核心：host 与 arm64 双架构构建、初版包装载、协议 TDD、板端原生 M146 双 PC 经**公网 S2** ICE/DTLS-SRTP 打通、双/三客户端错峰 440Hz 合成音频往返无削波/丢包 —— 已通过（合成音频，非真人）。
- C5（2026-10-20）：P2P Peer、`room.mediaMode` 先建后断、第三人切 SFU 后重新发布、`call.*` 与 Qt 呼叫/接听/挂断、TURN 提前刷新；host `ctest` 3/3、arm64 交叉编译通过，公网 S3 信令联调（`s3-smoke.py --audio none`）P2P 模式/先切后断/呼叫 20 项检查全过；**板端合成音频 P2P 与路径判定待开发板恢复后补测**（2026-10-20 晚 `172.16.123.11` SSH 不可达）。
- 已修复：同瞬间并发入房 `sfu.publish 409`（服务端 `44e9f6d`；2026-10-20 连续 8 轮 `stagger=0` 双/三端通过，每房间 1 个 Router 且创建/关闭配对）。
- 未收口：C3 真实声卡与 AEC；C5 板端媒体验证；C6–C7 依赖 S4/S5。
- **职责边界**：界面验收（实体屏幕显示/分辨率/触摸响应）**由开发者自行处理**，不纳入本客户端工作线验收范围；本线仅保证离屏渲染、中文字体与 Wayland 启动环境就绪（`run-qt.sh`）。
- 联调：`client/scripts/s2-smoke.py`（SFU）、`client/scripts/s3-smoke.py`（两人 P2P、第三人切 SFU、呼叫）；dev-token 运行时获取，凭据只存内存/stdin，不落日志/发布包。
  - 板端：先用 `client/scripts/package.sh` 生成 `build/rk3576/package` 并传到板子，再执行 `s3-smoke.py --server http://47.117.159.193:8080 --ssh root@<板子> --cli <板上 talk-cli> --dev-token`；无媒体信令联调可用 `--audio none` 在本机跑。
