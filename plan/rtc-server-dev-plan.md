# RTC 对讲服务：开发环境与开发计划

> 配套文档：[项目计划](./project-plan.md) · [信令协议](./talk-protocol.md) · [RK3576 libwebrtc 编译计划](../rk3576-libwebrtc-build-plan.md)
> 本文只覆盖**服务端（talk-server + mediasoup + coturn）**的开发、部署与联调；客户端核心库 talk-core 见项目计划。

## 1. 两台机器现状（2026-10-18 实测）

| 项 | 开发机 172.16.21.21 | 测试机 47.117.159.193 |
|---|---|---|
| 位置 | 公司内网 | 阿里云上海，`ecs.e-c1m1.large` |
| 系统 | Ubuntu 20.04.3，glibc 2.31 | Ubuntu 24.04.2，内核 6.8，glibc 2.39 |
| CPU / 内存 | 8 核 / 31GB | **2 核 / 1.6GB，无交换分区** |
| 磁盘 | 剩余 218GB（多人共用） | 40GB，剩余 35GB |
| 网络 | 内网，能访问 GitHub、npm | 公网 IP 由阿里云 NAT 映射（网卡上只有内网地址 172.24.53.130）；能访问 GitHub、npm、npmmirror |
| Node | v10.19（太旧，npm 已损坏） | 未安装 |
| Docker | 20.10.14，wdz 不在 docker 组 | 未安装 |
| 编译工具 | gcc 9.4、cmake 3.28、python 3.8 | gcc 13.3、python 3.12，无 cmake |
| 防火墙 | — | ufw 未启用，iptables 全部放行；**端口由阿里云安全组控制** |
| 现有服务 | 多人共用，有 VS Code / Cursor 远程服务端在跑 | 只有 sshd 和阿里云代理，环境干净 |

## 2. 结论一：在开发机上开发，测试机只做部署和公网联调

| 维度 | 开发机 | 测试机 |
|---|---|---|
| 资源 | 8 核 31GB，编译、跑多个测试机器人、开 IDE 都够用 | 1.6GB 内存：Cursor/VS Code 远程服务端本身就要 0.5~1GB，再加 TypeScript 语言服务、编译、mediasoup，很容易 OOM |
| 环境纯净 | 可以随意装开发依赖 | 应该保持"和上线一致"，只放运行时 |
| 网络 | 和 RK 板、Windows 测试机在同一内网，局域网联调方便、延迟低 | 有公网 IP，能验证 NAT 穿透、TURN、跨运营商、4G |
| 风险 | 共用机器，注意别占满磁盘 | root 直接开发，误操作影响面大 |

做法如下：

- **日常开发**：在开发机上开发。用 Cursor 的 Remote-SSH 连到开发机，代码放在 `/home/wdz/projects/rtc-talk`，服务跑在一个和测试机系统一致的 **Ubuntu 24.04 + Node 22 容器**里。在局域网内用测试机器人和 Windows 客户端联调。
- **测试机**：只做两件事。一是**部署**，每个可用版本通过脚本一键发布上去；二是**公网联调**，测试 3~4 人跨网络对讲、TURN 中继和弱网。
- **为什么开发机上要用容器**：开发机的 glibc 是 2.31，太旧，直接在宿主机上装 Node 22 和 mediasoup 容易出问题。容器里的系统和测试机一致（24.04 / glibc 2.39），**开发机上跑通的东西到测试机上行为一致**，打出来的部署包也可以直接用。测试机保持 Ubuntu 24.04，不重置为 22.04。
- **mediasoup worker 统一用官方 `kernel6` 预编译包**：mediasoup 按宿主内核主版本选预编译包（`mediasoup-worker-<版本>-linux-x64-kernel<N>.tgz`），官方只提供 kernel6（在 Ubuntu 22.04 上编译，glibc 2.35）和 kernel7。容器共用宿主内核，开发机是 5.15，没有对应的包，默认会在容器里从源码编译。因此安装时设 `MEDIASOUP_SKIP_WORKER_PREBUILT_DOWNLOAD=true`，由脚本手动下载 kernel6 包，再用 `MEDIASOUP_WORKER_BIN` 指向它。开发容器、部署包和测试机（内核 6.8）运行的是同一个 worker 二进制。

## 3. 结论二：测试机环境配置

目标：支撑 3~4 人语音（含视频预留）对讲，常驻内存控制在 500MB 以内。

### 3.1 资源估算（4 人）

| 组件 | 内存 | CPU | 带宽 |
|---|---|---|---|
| talk-server（Node） | 约 100MB | 很低 | 信令可忽略 |
| mediasoup worker ×1 | 约 50~80MB | 4 人语音 < 5% | 语音：4 路上行 × 3 路下行 × 约 40kbps ≈ 0.5Mbps |
| coturn | 约 20MB | 很低 | 只在 P2P 走中继时占用 |
| 合计 | **约 200~300MB** | 2 核绰绰有余 | 视频预留：4 人 720p（1.5Mbps）全订阅时出口约 18Mbps |

实例公网带宽：阿里云标称 10~30Mbps 突发，不限流量，高峰期约 5Mbps（未实测）。4 人语音约 0.5Mbps，余量充足；视频阶段按 5Mbps 保底算，4 人全订阅只能到约 400kbps/路（如 360p），要上 720p 需要升配带宽。S0 用 iperf3 实测一次上下行。

### 3.2 阿里云安全组放行（需要你在控制台操作）

| 方向 | 协议 | 端口 | 用途 | 来源建议 |
|---|---|---|---|---|
| 入 | TCP | 22 | SSH | 限制为公司出口 IP |
| 入 | TCP | 8080 | talk-server 信令，测试期用 `ws://47.117.159.193:8080` | 0.0.0.0/0 |
| 入 | UDP + TCP | 44444 | mediasoup WebRtcServer 单端口（TCP 用来兜底屏蔽 UDP 的网络） | 0.0.0.0/0 |
| 入 | UDP + TCP | 3478 | coturn STUN/TURN | 0.0.0.0/0 |
| 入 | UDP | 49160-49200 | coturn 中继端口（4 人足够） | 0.0.0.0/0 |

管理接口和 Prometheus 指标端口（9100）只监听 127.0.0.1，不对外开放。

### 3.3 系统与组件

| 项 | 方案 |
|---|---|
| 账户 | 用普通用户 `talk` 运行服务（已建好）；SSH 改为密钥登录，关闭 root 和密码登录，装 fail2ban |
| 交换分区 | 加 2GB 的 `/swapfile`，`vm.swappiness=10`，防止 npm 安装或突发内存时 OOM |
| 内核参数 | `net.core.rmem_max/wmem_max = 8MB`（UDP 收发缓冲），`fs.file-max`，ulimit nofile 65535 |
| Node | Node 22 LTS 官方二进制，装到 `/opt/node`（从 npmmirror 下载） |
| mediasoup | worker 跟着部署包走（官方 kernel6 预编译包，见第 2 节），**不在测试机上安装或编译** |
| coturn | `apt install coturn`（24.04 自带 4.6.x）；`use-auth-secret` + `static-auth-secret`（和 talk-server 共享）；`listening-ip=172.24.53.130`，`external-ip=47.117.159.193/172.24.53.130`；`min-port=49160`，`max-port=49200`；`no-tls`、`no-cli`；`denied-peer-ip` 屏蔽内网网段，防止被当作跳板访问阿里云内网 |
| mediasoup 公网地址 | 机器在 NAT 后面，所以 `WebRtcServer` 监听 `0.0.0.0:44444`，`announcedAddress=47.117.159.193` |
| 进程管理 | systemd：`talk-server.service`（`Restart=always`，`MemoryMax=600M`）、`coturn.service` |
| 目录 | `/opt/talk/releases/<版本>/` + `/opt/talk/current` 软链（方便回滚）；配置放 `/etc/talk/talk-server.env`；日志用 journald，再加一份按天切分的 pino 文件日志 |
| 不装的 | Docker（太吃内存）、Nginx（测试期直接用 `ws://IP`）、Redis（单节点用内存实现，接口按 Redis 设计） |

以上全部写进 `deploy/setup-test-server.sh`，可重复执行。以后换机器或上正式环境时，一条命令就能装好。

### 3.4 部署流程

```
开发机容器：npm ci && npm run build && npm prune --omit=dev
           → 打包 talk-server-<git短哈希>.tar.gz（含 dist、node_modules、mediasoup worker 二进制）
deploy.sh： scp 上传到测试机 → 解压到 releases/<版本> → 切换 current 软链 → systemctl restart talk-server → 健康检查 /healthz
回滚：      deploy.sh --rollback（current 指回上一个版本）
```

以后接入 GitHub Actions：push 到 `main` 时，在 ubuntu-24.04 runner 上执行 lint、测试和打包，可选自动部署到测试机（需要把 SSH 部署密钥存进 Secrets）。

## 4. 结论三：开发环境与开发过程

### 4.1 开发环境（开发机）

| 项 | 方案 |
|---|---|
| 编辑 | Cursor Remote-SSH 连 `wdz@172.16.21.21`，打开 `/home/wdz/projects/rtc-talk` |
| 运行时 | `docker compose` 起 `wdz-talk-dev` 容器（`ubuntu:24.04` + Node 22，与测试机一致），把源码目录挂进去，使用宿主网络（`network_mode: host`，避免 mediasoup 的 UDP 端口映射问题）。开发机 Docker 20.10 没有 compose 插件，装 compose v2 到 `~/.docker/cli-plugins/`（不需要 sudo） |
| 共用 Docker | 系统级 Docker 守护进程和 huangzuhong 共用，容器名和 compose 项目名统一加 `wdz-` 前缀，避免误操作对方的容器 |
| 本地 coturn | 同一个 compose 里起 `wdz-coturn`（`coturn/coturn` 镜像），用于 P2P 和 TURN 的本地测试 |
| 端口 | 开发机上 8080、44444、3478 可能被其他同事占用，所以端口全部可配置，默认用 18080、44444、13478 |
| 公网地址 | 局域网测试时 `announcedAddress=172.16.21.21` |
| Docker 权限 | 把 wdz 加入 docker 组：`sudo usermod -aG docker wdz`，然后重连 Remote-SSH。wdz 本身已在 sudo 组，不引入新的权限风险。不用 rootless Docker：它的 host 网络不是宿主真实网卡，mediasoup 和 coturn 的 UDP 联调会出问题 |

### 4.2 技术栈

| 用途 | 选型 |
|---|---|
| 语言 / 运行时 | TypeScript 5 + Node 22 LTS，ESM |
| SFU | `mediasoup` v3（最新稳定版） |
| WebSocket | `ws` |
| 消息校验 | `zod`（每个 method 一个 schema，对应协议文档） |
| SDP | `sdp-transform` + 从 mediasoup-client 移植的 `ortc` / `RemoteSdp`（服务端 SDP 适配） |
| 鉴权 | `jose`（JWT）；测试期提供 `/dev/token` 接口签发测试 token，只在开发配置下启用 |
| 日志 | `pino` |
| 指标 | `prom-client` |
| 测试 | `vitest`（单元测试）+ 测试机器人（集成测试） |
| 代码规范 | eslint + prettier，husky pre-commit |

### 4.3 测试工具

服务端开发不能等客户端核心库做完才联调，所以服务端自己配三类测试客户端：

1. **测试机器人 `tools/bot`**（Node 无头客户端）：用 `@roamhq/wrtc`（node-webrtc 的维护分支，内部就是 libwebrtc，只说标准 SDP），按协议入房、发正弦波或语音文件、收音频并统计丢包和抖动。用法如 `npm run bot -- --room r1 --count 4`，一条命令模拟 4 人。如果它装不上，备选纯 TS 实现的 `werift`。
2. **Windows 命令行客户端 `talk-cli`**：基于现有的 `webrtc_sdk_hevc`，用真实声卡验证回声和音质，也是 talk-core 的第一个雏形。
3. **网页调试页 `tools/web-debug`**（可选）：方便肉眼观察。在 `http://IP` 下浏览器不允许用麦克风，需要用 Chrome 的 `--unsafely-treat-insecure-origin-as-secure` 参数或者自签证书。正式的浏览器接入仍然放到二期。

### 4.4 仓库结构（monorepo）

```
rtc-talk/                   # github.com/feng9201/rtc-talk
├── server/                 # talk-server
│   ├── src/
│   │   ├── gateway/        # WS 连接、消息封装（req/res/ntf）、心跳、限流
│   │   ├── auth/           # JWT、TURN 临时凭证
│   │   ├── session/        # 会话、断线恢复
│   │   ├── room/           # 房间、成员、策略、通知
│   │   ├── media/          # worker 池、Router、WebRtcServer、transport/producer/consumer
│   │   │   └── sdp/        # SDP 适配（offer→produce，consumer→recv offer，m-line 复用）
│   │   ├── call/           # 1v1 呼叫、P2P 信令转发、ModePolicy、P2P↔SFU 迁移
│   │   ├── floor/          # PTT 发言权、主持人管控
│   │   ├── metrics/        # Prometheus、管理 API
│   │   └── config/
│   └── test/
├── tools/
│   ├── bot/                # 无头测试机器人
│   └── web-debug/          # 网页调试页（可选）
├── client/                 # talk-core（C++，后续），talk-cli（Windows）
├── deploy/
│   ├── docker/             # 开发容器、compose
│   ├── systemd/            # talk-server.service
│   ├── coturn/             # turnserver.conf 模板
│   ├── setup-test-server.sh
│   └── deploy.sh
├── docs/                   # 现有的 project-plan、talk-protocol 等迁移进来
└── .github/workflows/      # CI
```

### 4.5 日常开发循环

```
Cursor 改代码 → 容器里 tsx watch 热重载 → bot / talk-cli 在局域网验证
   → git commit → push 到 GitHub（PR 时 CI 跑 lint、测试）
   → 合入 main → deploy.sh 发布到测试机 → 3~4 人公网实测（含 4G 热点、跨运营商）
```

### 4.6 开发阶段（服务端）

工期按"我主导编码、你负责决策和真人实测"的节奏估算，不是人工团队的工期。真人联调需要凑人和设备，实际进度主要看这一环。

| 阶段 | 内容 | 验收 | 预计 |
|---|---|---|---|
| **S0 环境** | GitHub 仓库与脚手架；开发容器；测试机初始化（`setup-test-server.sh`）；安全组；coturn 上线 | Windows 上用 Trickle ICE 工具能拿到测试机的 srflx 和 relay 候选；`/healthz` 返回正常 | 0.5~1 天 |
| **S1 信令骨架** | 消息封装、心跳、错误码；`session.*`；开发用 JWT；`room.join/leave` 和成员通知 | 单元测试覆盖消息封装和房间状态；2 个 bot 能互相看到进出房间 | 1~2 天 |
| **S2 SFU 语音 MVP** | worker 池、Router、WebRtcServer；SDP 适配（send PC → produce，recv PC → 全量 consumer）；开关麦；入房合并协商 | 测试机上 4 个 bot 互通音频，丢包为 0；`talk-cli` 和 bot 双向能听到；局域网端到端延迟 < 300ms | 3~5 天 |
| **S3 P2P 与模式切换** | `call.*`、`p2p.*` 转发；TURN 临时凭证；ModePolicy；P2P→SFU 先建后断切换 | 内网直连、跨 NAT、强制只走中继三种场景都能接通；第 3 人加入时 P2P 切到 SFU，断音 < 500ms | 3~4 天 |
| **S4 对讲形态** | `floor.*`（PTT 抢麦、排队、超时收回）；`host.*`；发言人和音量通知 | PTT 按下到出声 < 300ms；主持人静音和踢人生效 | 2 天 |
| **S5 稳定性** | 断线恢复（resume）、ICE restart、网络切换；指标；弱网测试（tc netem）；部署脚本和文档 | 拔网线或切 Wi-Fi 后 5 秒内恢复；20% 丢包时语音仍可懂；24 小时长稳无内存泄漏 | 2~3 天 |

服务端合计约 **2~3 周**能达到"测试机上稳定支撑 3~4 人对讲"。客户端 talk-core（Windows → RK → macOS）和 RK3576 的 libwebrtc 编译并行推进。S2 一完成，客户端就可以接入真实服务端。

### 4.7 测试机上的"完成"标准（第一里程碑）

- 3~4 名同事分别用 Windows `talk-cli` 和 bot，跨公司网络、家庭宽带、4G 热点，同房间全双工通话 30 分钟，没有断线和明显回声；
- 两人房间默认走 P2P，第 3 人加入时自动切到 SFU；
- 测试机上服务常驻内存 < 400MB，CPU < 20%。

## 5. 风险

| 风险 | 对策 |
|---|---|
| 测试机 1.6GB 内存，`npm ci` 或突发时 OOM | 部署包在开发机打好；测试机只解压、不安装；加 2GB 交换分区；systemd 设置 `MemoryMax` |
| 阿里云 NAT 导致 ICE 候选错误 | mediasoup 的 `announcedAddress` 和 coturn 的 `external-ip` 都显式配置公网 IP；S0 用 Trickle ICE 验证 |
| 开发机多人共用，端口冲突、磁盘紧张 | 端口全部可配置；容器和构建产物定期清理；仓库放在 `/home/wdz/projects` 下 |
| `@roamhq/wrtc` 装不上或不稳定 | 换成 `werift`；最差情况用 talk-cli 做主力测试 |
| 测试机 root 密码已经在聊天中明文出现 | 初始化完成后改成密钥登录，并修改 root 密码 |
| 公网带宽不足（视频阶段） | 高峰约 5Mbps，只够 4 人低分辨率视频；视频阶段降分辨率或升配带宽 |

## 6. 需要你提供或确认

### 6.1 已确认（2026-10-19）

| 事项 | 结论 |
|---|---|
| 鉴权与房间 | 一期用开发 token（`/dev/token`）；房间由客户端在 `room.join` 时即时创建（见协议 5.2） |
| 客户端 UI | Qt |
| 录制存档 | 一期不做 |
| 正式部署环境 | 暂不考虑 |
| 信令传输 | 测试期 `ws://47.117.159.193:8080`，不上 TLS |
| RK 平台 | 只支持 RK3576 |
| 测试机系统 | 保持 Ubuntu 24.04；mediasoup worker 统一用官方 kernel6 预编译包（见第 2 节） |
| 开发机 Docker | wdz 已加入 docker 组（重连 Cursor 后生效），不用 rootless Docker（见 4.1） |
| Cursor Remote-SSH | 已连上开发机 |
| 代码仓库 | GitHub [feng9201/rtc-talk](https://github.com/feng9201/rtc-talk)，本地目录 `/home/wdz/projects/rtc-talk` |
| 阿里云安全组 | 已放行；从开发机探测 TCP 22 通，8080 / 44444 / 3478 返回 connection refused（已放行、尚无服务监听）。UDP 端口等服务起来后再验证 |
| 公网带宽 | 10~30Mbps 突发，高峰约 5Mbps，不限流量（见 3.1） |
| 测试机账户 | `talk` 用户已建好（在 sudo 组，sudo 需要密码），root 密码已改；开发机 wdz 的公钥已加入 `talk` 的 `authorized_keys`，可免密登录 |

### 6.2 仍需提供

| # | 事项 | 用途 | 方式 |
|---|---|---|---|
| 1 | **GitHub 推送权限** | 推送代码、CI | 把开发机 wdz 的公钥（`~/.ssh/id_rsa.pub`）加到仓库 Settings → Deploy keys，勾选 Allow write access |
| 2 | 关闭测试机 SSH 密码登录前，提供你本机的 SSH 公钥 | 防止关闭后你自己登不上 | 发给我即可 |
| 3 | 3~4 名测试同事和设备（Windows 电脑 + 耳机 + 4G 热点） | S2 之后的真人联调 | 到时候约时间 |
| 4 | RK3576 板子（SSH 地址、账号）及音频 codec 型号、是否带硬件回采 | RK 上板验证、回声消除方案 | P3 之前提供 |
| 5 | Windows 开发机及 `webrtc_sdk_hevc` 的访问方式 | 编译 `talk-cli` | S2 之前提供 |
