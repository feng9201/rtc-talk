# 环境、账号与进度速查

> 给接手人或换机器时用。口令只写在测试机用途上，**不要**把测试机密码同步到正式环境。
> 更新：2026-10-19（S1 完成后）

## 1. 当前进度

| 阶段 | 状态 | 说明 |
|---|---|---|
| **S0 环境** | ✅ 完成 | 开发容器、coturn、测试机初始化、打包/发布/回滚、TURN 验证、`/healthz` 公网可达 |
| **S1 信令骨架** | ✅ 完成 | WS 信令、JWT、房间进出与成员通知、断线恢复、顶号；38 个用例；bot 公网验收通过 |
| **S2 SFU 语音 MVP** | ⏭ 下一步 | worker 池已有，待接 Router / WebRtcServer、SDP 适配、produce/consume、开关麦；bot 真正互通 Opus |
| S3 P2P 与模式切换 | 未开始 | — |
| S4 对讲形态（PTT/host） | 未开始 | — |
| S5 稳定性 | 未开始 | — |

**已发布到测试机**：`abea707`（S1 代码）；文档提交 `85f010a`。  
**健康检查**：`http://47.117.159.193:8080/healthz`  
**信令**：`ws://47.117.159.193:8080/talk/v1/ws`（子协议 `talk.v1.json`）  
**开发 token**：`GET http://47.117.159.193:8080/dev/token?uid=xxx&rooms=room1`（仅 `TALK_ENV=dev`）

### 下一步（S2）要做的事

1. 房间绑定 mediasoup Router + WebRtcServer（单端口 `TALK_RTC_PORT=44444`）。
2. 实现协议第 6 节：`sfu.publish`、服务端 `sfu.offer`（全量订阅）、ICE/DTLS、`media.setMute` 真正 pause/resume Producer。
3. SDP 适配（服务端用 mediasoup-client 的 ortc / RemoteSdp 思路，客户端只说标准 SDP）。
4. 扩展 `tools/bot`：用 `@roamhq/wrtc` 或 `werift` 发 Opus，验证 4 bot 互通、0 丢包。
5. 发布到测试机，公网验收；`talk-cli` 接入留到客户端侧并行。

详细阶段表见 [rtc-server-dev-plan.md §4.6](./rtc-server-dev-plan.md)。

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
| RK3576 板子 SSH、音频 codec、是否硬件回采 | RK 上板 / P3 前 |
| Windows 开发机与 `webrtc_sdk_hevc` 访问方式 | 编 `talk-cli` / S2 前尽量给 |

---

## 7. 文档索引

| 文档 | 内容 |
|---|---|
| [project-plan.md](./project-plan.md) | 需求、选型、整体架构 |
| [talk-protocol.md](./talk-protocol.md) | 信令协议 v1 |
| [rtc-server-dev-plan.md](./rtc-server-dev-plan.md) | 服务端开发计划与 S0/S1 执行记录 |
| [rk3576-libwebrtc-build-plan.md](./rk3576-libwebrtc-build-plan.md) | RK3576 libwebrtc 编译 |
| 本文 | 进度、下一步、机器账号、公钥、仓库 |
