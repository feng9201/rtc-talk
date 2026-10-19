# RK3576 libwebrtc（M146）交叉编译计划

> 目标：在 172.16.21.21 上为 RK3576 交叉编译 libwebrtc，体积尽可能小。要求支持 H.264/H.265 传输和音频核心功能（采集播放、3A、Opus、NetEQ、拥塞控制、ICE/DTLS-SRTP）。
> 源码版本与 Windows 一致：branch-heads/7680（M146）@ `12cce13db567fd1e284a4b7821b46d63f8e674a6`，参考 `D:\google\webrtc.md`。

## 1. 环境调查结论（2026-10-16）

### 1.1 编译主机

| 项 | 实测 | 影响 |
|---|---|---|
| 系统 | Ubuntu 20.04.3 LTS，x86_64，内核 5.15 | M146 的预编译工具（clang、siso、rust）可能要求更新的主机 glibc，见风险 R1 |
| 主机 glibc | 2.31 | 同上 |
| CPU / 内存 | 8 核 / 31GB | 全量编译预计 1~2 小时 |
| 磁盘 | 编译目录 `/home/wdz/projects/hxf/webrtc_m146` 所在分区（`/dev/vda5`）剩余 222GB（已用 89%） | WebRTC 源码 + Linux arm64 依赖约 30GB，够用但要留意 |
| 外网 | 直连 `webrtc.googlesource.com` 返回 200 | 可直接 `fetch`，不需要代理 |
| 工具 | python3 3.8、git 2.25、clang 10、ninja、docker 20.10；无 gn/depot_tools | depot_tools 自带 python 与 gn；主机 clang 10 太旧，不使用 |
| Docker | 已装；用户 wdz 不在 docker 组，需要 sudo | 暂不使用（见风险 R1），直接在主机上编译 |
| 链接器 | Buildroot 工具链只有 `ld.bfd`，没有 gold / lld | GCC 应用链接时不能做 ICF，也不能吃 clang 的 LTO bitcode，见 2.2 |

### 1.2 RK3576 交叉编译环境（`/opt/rockchip/rk3576/host`）

| 项 | 实测 |
|---|---|
| 来源 | Buildroot（软链到 `/home/zhangtianyi/project/rk3576/buildroot/output/rockchip_rk3576/host`） |
| 编译器 | `aarch64-buildroot-linux-gnu-gcc` 12.4.0，`--with-cpu=cortex-a72.cortex-a53`，`--with-abi=lp64` |
| sysroot | `host/aarch64-buildroot-linux-gnu/sysroot` |
| 板端 glibc | 2.38 |
| 板端 libstdc++ | `libstdc++.so.6.0.30`（GCC 12） |
| libgcc / crtbegin | 在 `host/lib/gcc/aarch64-buildroot-linux-gnu/12.4.0`，**不在 sysroot 内** |
| libstdc++ 头文件 | **不在 sysroot 的 `usr/include/c++` 下**（在工具链目录内） |
| 音频 | `libasound`（ALSA）+ 头文件、`libpulse` / `libpulse-simple` + 头文件 |
| 加密 | OpenSSL 3.2.1 |
| 硬件编解码 | `librockchip_mpp`（+ `rockchip/` 头文件、`rockchip_mpp.pc`）、`librga` |
| 图形 | Wayland、EGL、GLES、GBM、DRM；**无 X11** |
| 其他 | glib、dbus、udev；无系统 opus / libvpx |

### 1.3 现有应用

`~/webrtc_back` 是一个基于 **libdatachannel**（`rtc/rtc.hpp`）+ miniaudio + 自写 Opus 封装 + 自写 NACK 的客户端，没有回声消除和 NetEQ，和 libwebrtc 无关。这次的 libwebrtc 用来替换它的媒体层。

## 2. 关键决策

### 2.1 工具链：Chromium 自带 clang + Buildroot sysroot（方案 A，推荐）

| 方案 | 做法 | 优点 | 缺点 |
|---|---|---|---|
| **A（推荐）** | Chromium 自带 clang 23；`target_sysroot` 指向 Buildroot sysroot；`use_custom_libcxx=false`，使用板端 libstdc++ 12 | 直接链接板端真实的 glibc 2.38、libstdc++、ALSA、Pulse、MPP；**不额外打包 libc++，体积最小**；GCC 12 编译的应用可以直接用 C++ 接口 | Buildroot 的 sysroot 布局和 clang 预期的不同，要做适配（见 3.2） |
| B（备选） | Chromium 自带 clang + 官方 Debian bullseye arm64 sysroot（glibc 2.31）+ 内置 libc++（静态、隐藏符号） | 官方支持的路径，最稳；glibc 2.31 编出来的程序能在 2.38 的板子上运行 | 多出约 0.5~1MB 的 libc++；只能通过 C 接口对外，GCC 编译的应用不能直接用 C++ 接口 |
| C（不采用） | Buildroot GCC 12 直接编 libwebrtc | 工具链统一 | WebRTC 官方只测 clang，M146 用 GCC 编大概率要逐个修错，维护成本高 |

P1 阶段先用 1 天验证方案 A。如果 sysroot 适配解决不了，就切换到方案 B，GN 参数中只有 sysroot 和 libc++ 两项不同。注意方案 B 下 GCC 应用无法直接链接 C++ 接口，交付形式只能用 2.2 的乙。

### 2.2 体积策略与交付形式

`libwebrtc.a` 本身体积没有意义（Windows Release 版就有 150MB），最终程序的大小取决于实际引用了哪些代码。体积靠三层手段控制：

1. **GN 参数**：不编译用不到的模块（见 3.3），比如 protobuf、SCTP、桌面采集、AV1、软件 H.264、VP9。
2. **链接**：`-ffunction-sections` + `-Wl,--gc-sections` 删除未引用的代码，`symbol_level=0`，最后 `strip`。
3. **只注入需要的编解码器**：音频只用 Opus 模板；视频只注入 MPP 的 H.264/H.265 工厂。libvpx 等内置编解码器不被引用，链接时就会被丢掉（Windows 上已实测：不用 VP8/VP9 时最终 EXE 里 libvpx 目标文件为 0）。

两种交付形式的体积差异：

| 形式 | 最终链接方 | 能用的手段 |
|---|---|---|
| **甲：`libwebrtc.a` + 头文件（暂定）** | 应用用 Buildroot GCC 12 + `ld.bfd` 静态链接 | `--gc-sections`；**不能用 ThinLTO**（`ld.bfd` 读不了 clang 的 bitcode，所以 `.a` 必须 `use_thin_lto=false` 编译）；**不能用 ICF**（`ld.bfd` 不支持） |
| 乙：`libtalk_core.so` + C 接口 | 在 WebRTC 源码树内用 clang + lld 链接 | 甲的全部 + ThinLTO + `--icf=all` + 版本脚本只导出 `talk_*` |

删除未引用代码这一步两者效果相同（静态链接进可执行文件时，未被 `main` 引用到的代码同样会被 `--gc-sections` 删掉），差别只来自 ThinLTO 和 ICF。按经验 ThinLTO 约省 3~8%、ICF 约省 3~5%，在 6~8MB 的基数上**估计差 0.5~1.2MB，小于 2MB，因此暂定采用甲**。这只是估算，P2 用同一个探针实测两种形式的体积（见 3.4），差异超过 2MB 就改用乙。

甲的接入约束（应用侧要遵守，交付时写进 SDK 说明）：
- 应用必须用和 libwebrtc 相同的宏：`WEBRTC_POSIX`、`WEBRTC_LINUX`、`RTC_ENABLE_H265` 等，以 `gn desc` 导出的 defines 为准，SDK 里提供一份 `webrtc_defines.cmake`。
- `-std=c++20`；libwebrtc 用 `-fno-rtti` 编译，应用继承 WebRTC 接口类（如 `PeerConnectionObserver`）的源文件也要加 `-fno-rtti`，否则链接时报 `undefined reference to typeinfo for ...`。如果应用离不开 RTTI，GN 加 `use_rtti = true`（体积略增）。
- 链接时加 `-Wl,--gc-sections`，并按依赖顺序链接 `libwebrtc.a`、`-lpthread -ldl -lm`。

体积目标：**纯音频 + H.264/H.265 传输的应用（含 libwebrtc）在 strip 后 ≤ 8MB**，P2 实测后再调整。

### 2.3 保留与裁剪

| 保留（核心功能） | 裁剪 |
|---|---|
| ALSA / PulseAudio 音频设备（运行时 dlopen） | X11、PipeWire、GTK、桌面采集 |
| APM：AEC3、NS、AGC2、高通滤波 | protobuf（RtcEventLog 序列化、AEC dump） |
| Opus（含 FEC、DTX）；NetEQ | iLBC、G722 等其他音频编解码器（不注入，链接时丢弃） |
| RTP/RTCP、NACK、TWCC/GCC 拥塞控制 | SCTP / DataChannel（需要时再开） |
| ICE / STUN / TURN、DTLS-SRTP（BoringSSL） | 内置根证书（改用系统 `/etc/ssl/certs`） |
| H.264、**H.265** 的 RTP 打包/解包与 SDP 协商 | 软件 H.264（OpenH264/FFmpeg）、AV1、VP9 |
| 视频引擎主干（H.264/H.265 传输依赖它） | trace events |
| 编解码器工厂注入点（接 MPP） | 测试、示例、工具 |

## 3. 实施步骤

### 3.1 获取源码（主机 172.16.21.21）

编译根目录为本文档所在目录 `/home/wdz/projects/hxf/webrtc_m146`（下文记为 `$ROOT`），源码、depot_tools、sysroot 适配目录都放在它下面。

```bash
export ROOT=/home/wdz/projects/hxf/webrtc_m146
cd $ROOT
git clone https://chromium.googlesource.com/chromium/tools/depot_tools.git
export PATH=$ROOT/depot_tools:$PATH
fetch --nohooks webrtc
cd src && git checkout --detach 12cce13db567fd1e284a4b7821b46d63f8e674a6 && cd ..
# .gclient 中设置 target_os = ["linux"]，只拉 Linux 依赖
gclient sync -D
python3 src/build/linux/sysroot_scripts/install-sysroot.py --arch=arm64   # 方案 B 需要；方案 A 也用来对比验证
```

- 把 Windows 上 `src/BUILD.gn` 的 AV1 门控补丁（13+/6-）应用过来。
- 先在主机上运行一下 `gn --version`、`src/third_party/llvm-build/Release+Asserts/bin/clang --version`、`siso version`。如果因为主机 glibc 2.31 太旧跑不起来，按风险 R1 处理。

### 3.2 适配 Buildroot sysroot（方案 A）

不修改原始的 Buildroot 目录，另外建一个适配目录 `$ROOT/rk3576_sysroot`：

```bash
SR=/opt/rockchip/rk3576/host/aarch64-buildroot-linux-gnu/sysroot
H=/opt/rockchip/rk3576/host
DST=$ROOT/rk3576_sysroot
rsync -a $SR/ $DST/
# clang 按 aarch64-linux-gnu 的路径找 GCC 安装：crtbegin.o、libgcc
mkdir -p $DST/usr/lib/gcc/aarch64-linux-gnu
ln -s $H/lib/gcc/aarch64-buildroot-linux-gnu/12.4.0 $DST/usr/lib/gcc/aarch64-linux-gnu/12
# libstdc++ 头文件：放到 clang 预期的 usr/include/c++/12 下，并补上三元组目录别名
mkdir -p $DST/usr/include/c++
ln -s $H/aarch64-buildroot-linux-gnu/include/c++/12.4.0 $DST/usr/include/c++/12
ln -s aarch64-buildroot-linux-gnu $DST/usr/include/c++/12/aarch64-linux-gnu
```

验证：用 Chromium clang 加 `--target=aarch64-linux-gnu --sysroot=$ROOT/rk3576_sysroot` 编一个 C++20 的 hello world，确认 `file` 输出为 aarch64，`readelf -V` 里要求的 GLIBC/GLIBCXX 版本不超过板端版本，并在板子上运行通过。

### 3.3 GN 参数（`out/rk3576_release/args.gn`）

```gn
target_os = "linux"
target_cpu = "arm64"
is_debug = false
is_component_build = false
is_clang = true
symbol_level = 0
treat_warnings_as_errors = false

# 工具链与 ABI（方案 A）
use_sysroot = true
target_sysroot = "/home/wdz/projects/hxf/webrtc_m146/rk3576_sysroot"
use_custom_libcxx = false
use_elf_crel = false               # 本地补丁新增的参数，见 7.2；甲形式必需

# 体积
use_thin_lto = false               # 交付形式甲：.a 给 GCC + ld.bfd 链接，不能是 bitcode；改用乙时设为 true
optimize_for_size = false          # P2 对比 -O2 与 -Os 的体积和 CPU 占用后再定

# 编解码与传输
rtc_use_h264 = false               # 不编 OpenH264 / FFmpeg；H.264 传输始终在
rtc_use_h265 = true                # H.265 打包/解包、协商、码流解析
rtc_libvpx_build_vp9 = false
enable_libaom = false
rtc_include_dav1d_in_internal_decoder_factory = false

# 音频
rtc_include_internal_audio_device = true
rtc_include_pulse_audio = true     # 运行时 dlopen，板上没有 Pulse 时自动回落到 ALSA
rtc_include_opus = true

# 裁剪
rtc_enable_protobuf = false
rtc_enable_sctp = false            # 暂不需要 DataChannel，需要时改 true 重编（约 +0.5~1MB）
rtc_use_x11 = false
rtc_use_pipewire = false
rtc_builtin_ssl_root_certificates = false
rtc_disable_trace_events = true
rtc_include_tests = false
rtc_build_examples = false
rtc_build_tools = false
use_glib = false
```

方案 B 改为：`target_sysroot` 删掉（使用官方 sysroot），`use_custom_libcxx = true`。

注意：
- `rtc_enable_protobuf=false` 后 AEC dump 不可用；调回声时如果需要，单独编一个开启 protobuf 的调试版。
- 日志（`rtc_disable_logging`）一期保留，便于现场排查；正式版再评估要不要关。
- RK3576 是 A72 + A53，ARMv8.0，不需要设置 `arm_control_flow_integrity`（PAC/BTI 在 ARMv8.0 上会退化成 NOP）。

### 3.4 编译与产物

1. `gn gen out/rk3576_release`，用 `gn desc //:webrtc defines` 确认包含 `RTC_ENABLE_H265`，并且没有 `WEBRTC_USE_H264`、`WEBRTC_ENABLE_PROTOBUF=1`。
2. `autoninja -C out/rk3576_release webrtc`，得到 `obj/libwebrtc.a`。
3. 写一个体积探针 `talk_probe.cc`（在客户端核心库就绪前用来量体积和上板验证）：
   - 创建 PeerConnectionFactory：ALSA/Pulse 音频设备、APM、Opus 模板、只声明 H.264/H.265 的视频工厂；
   - 两个本地 PeerConnection，音频走 loopback，同时验证 H.265 协商。
4. 用同一份 `talk_probe.cc` 量两种交付形式的体积：
   - 甲：在源码树外用 Buildroot GCC 12 编译探针，静态链接 `libwebrtc.a`（`-Wl,--gc-sections`），得到 `talk_probe_gcc`。这一步同时验证甲的接入约束（宏、`-fno-rtti`、libstdc++ ABI）是否可行。
   - 乙：另建 `out/rk3576_lto`（`use_thin_lto=true`），在源码树内建 GN `shared_library` 目标 `talk_core_probe`，版本脚本只导出 `talk_*`，加 `--icf=all`。
   - 两者都 `strip` 后比较。差异 ≤ 2MB 用甲，> 2MB 用乙。
5. 用 `readelf -d` 确认动态依赖只有 libc、libm、libpthread、libdl、libstdc++、libgcc_s（ALSA/Pulse 运行时 dlopen）。

### 3.5 板端验证

| 项 | 方法 | 标准 |
|---|---|---|
| 运行 | 拷贝 `talk_probe_gcc`（或乙的 `talk_core_probe.so` + 测试程序）到板子 | 正常启动，音频设备可枚举 |
| H.265 协商 | 测试程序内的 offer/answer | 含 `H264/90000`、`H265/90000` |
| 音频 loopback | 麦克风 → APM → Opus → 本地 PC → NetEQ → 扬声器 | 能听到声音，日志无欠载 |
| 回声消除 | 外放说话，录下远端收到的信号 | 无明显回声 |
| CPU | `top` 观察，分别绑定到 A53 小核和 A72 大核 | 单路通话 A53 单核 < 15% |
| 体积 | strip 后的探针大小 | ≤ 8MB |

## 4. 里程碑

| 阶段 | 内容 | 交付物 | 预计 |
|---|---|---|---|
| P1 环境 | depot_tools、源码同步、AV1 补丁、sysroot 适配、hello world 上板 | 可用的源码树；方案 A/B 结论 | 1 天（主要是同步源码的时间） |
| P2 编译与体积 | 按 3.3 的参数编译；GCC 链接探针（甲）与 LTO .so 探针（乙）的体积对比；对比 -O2/-Os | `libwebrtc.a`、`talk_probe_gcc`、体积对比表、交付形式结论 | 1~2 天 |
| P3 上板验证 | 3.5 全部项目 | 验证报告 | 1~2 天（板子信息后续提供） |
| P4 交付 | SDK 目录（include、lib、defines、manifest、示例）、构建脚本、`webrtc.md` 式的执行记录 | `webrtc_sdk_rk3576_arm64` | 0.5 天 |
| 后续 | MPP H.264/H.265 编解码器工厂（客户端核心库的一部分） | — | 1~2 周 |

## 5. 风险

| # | 风险 | 对策 |
|---|---|---|
| R1 | Ubuntu 20.04（glibc 2.31）跑不了 M146 的预编译 clang、siso 或 rust 工具 | **已排除**：全部工具在主机上可直接运行（见 7.1） |
| R7 | 甲形式下 GCC 12 编译 WebRTC M146 头文件报错，或 RTTI / 宏不一致导致链接失败 | **已解决**：唯一的问题是 CREL，用 `use_elf_crel=false` 解决（见 7.2、7.4） |
| R2 | Buildroot sysroot 适配后仍链接失败（crt、libgcc、头文件路径） | 切换到方案 B（官方 sysroot + 内置 libc++），对外只用 C 接口 |
| R3 | 关闭 protobuf 后某些目标编译失败 | 定位到具体目标；必要时保留 protobuf，只靠链接期删除未引用代码 |
| R4 | `-Os` 让 APM / Opus 的 CPU 占用明显升高 | 整体用 -O2，只对非热点模块用 -Os，或者干脆不开 |
| R5 | 板端 ALSA 设备名或采样率与默认值不符 | 客户端核心库提供设备选择；ADM 打开失败时打印可用设备 |
| R6 | 磁盘空间（剩 222GB） | 只同步 Linux 依赖，构建目录只保留一份 Release |

## 6. 确认结果（2026-10-16）

1. 编译根目录：`/home/wdz/projects/hxf/webrtc_m146`（本文档所在目录，wdz 可写，分区剩余 222GB）。暂不用 sudo 跑 Docker，按 R1 的顺序在主机上解决。
2. RK3576 板子：有，IP、账号和音频外设信息后续提供；P3 之前不阻塞。
3. DataChannel：暂不需要，`rtc_enable_sctp = false`；需要时打开重编。
4. 交付形式：乙比甲预计只小 0.5~1.2MB（见 2.2），**暂定甲（`libwebrtc.a` + 头文件）**；P2 实测差异超过 2MB 时改用乙（`libtalk_core.so` + C 接口）。**P2 实测差 0.78MB，确定用甲**（见 7.3）。

## 7. 执行记录（2026-10-16，P1、P2 完成）

### 7.1 环境搭建

```bash
source /home/wdz/projects/hxf/webrtc_m146/env.sh   # 设置 ROOT、PATH，去掉代理变量
```

| 步骤 | 实际情况 |
|---|---|
| 网络 | shell 环境里有 `http(s)_proxy=127.0.0.1:7897`，但代理没在运行；直连 googlesource / storage.googleapis.com 都正常。`env.sh` 里 unset 掉代理变量 |
| git | 主机 git 2.25 不支持 depot_tools 用的 `git init --object-format`（需要 ≥ 2.29）。用 `cipd install infra/3pp/tools/git/linux-amd64` 装了 git 2.56 到 `$ROOT/tools/git`，不需要 sudo |
| 源码 | 手写 `.gclient`，url 直接锁 `@12cce13d…`（即 branch-heads/7680 的 HEAD），`gclient sync --nohooks --no-history -D`，约 25 分钟；`gclient runhooks` 约 15 分钟。`src` 共 16GB |
| R1 | **未触发**：clang 23、lld、rustc 1.95、gn、siso 1.4.30 都能在 glibc 2.31 主机上直接运行，不需要 Docker |
| sysroot | 按 3.2 适配，GCC 安装目录和 libstdc++ 头文件用**复制**而不是软链（让适配目录自成一体），共 604MB。rsync 有一个无关目录 `data/lib/bluetooth` 没权限，忽略 |
| hello world | clang 23 + 适配 sysroot 编 C++20 程序成功；要求 GLIBC ≤ 2.34、GLIBCXX ≤ 3.4.22；在主机上用 `qemu-aarch64-static -L rk3576_sysroot` 运行通过。**方案 A 可行，不需要方案 B** |

注意：GCC 12 的 libstdc++ 没有 `<format>`（GCC 13 才有）；WebRTC 自身代码没用到它，但客户端代码也不能用。

### 7.2 本地补丁（已导出到 `$ROOT/patches/`）

| 文件 | 内容 |
|---|---|
| `src/BUILD.gn` | AV1 门控：dav1d / libaom 两个适配器只在 `rtc_include_dav1d_in_internal_decoder_factory` / `enable_libaom` 打开时加入；`//:webrtc` 的 visibility 加 `//talk_probe:*`；`default` 组加入探针目标 |
| `src/build/config/compiler/BUILD.gn` | 新增 GN 参数 `use_elf_crel`（默认 true）。Chromium 默认给 Linux 目标加 `-Wa,--crel`（CREL 压缩重定位），Buildroot 的 binutils 2.40 读不了，GCC 应用链接时报 `unknown architecture of input file` 和 `.eh_frame` 错误。**甲形式必须 `use_elf_crel = false`** |
| `src/talk_probe/` | 探针源码、GN 目标、只导出 `talk_*` 的版本脚本 |

### 7.3 编译与体积

| 构建目录 | 参数差异 | `libwebrtc.a` | 耗时 |
|---|---|---|---|
| `out/rk3576_release` | 3.3 参数 + `use_elf_crel=false` | 41MB，2496 个目标文件，无 AV1/OpenH264/protobuf/SCTP/rust | 约 5 分钟 |
| `out/rk3576_os` | 同上 + `optimize_for_size=true` | 45MB | 约 5 分钟 |
| `out/rk3576_lto` | `use_thin_lto=true`，CREL 保持默认 | — | 约 1.5 分钟（只链探针） |

`gn desc` 确认：有 `RTC_ENABLE_H265`、`WEBRTC_ENABLE_PROTOBUF=0`，无 `WEBRTC_USE_H264`。libvpx 的 VP9 目标文件仍在 `.a` 里（`rtc_libvpx_build_vp9=false` 只关掉 WebRTC 的 VP9 封装），但最终链接时全部被丢弃。

同一份探针（音频 + APM + Opus + 只声明 H.264/H.265 的视频工厂 + 两个 PeerConnection），strip 后：

| 形式 | 链接方式 | 体积 |
|---|---|---|
| **甲，-O2（采用）** | Buildroot GCC 12 + ld.bfd，`--gc-sections` | **7,514,776（7.17MB）** |
| 甲，-Os | 同上，库用 `out/rk3576_os` | 7,019,160（6.69MB） |
| clang 静态，-O2 | clang + lld，`--gc-sections` + ICF，无 LTO | 7,059,016（6.73MB） |
| 乙，-O2 | ThinLTO + ICF + 版本脚本，`.so` 6,682,744 + 启动程序 12,720 | 6,695,464（6.39MB） |

- 甲比乙大 0.78MB，小于 2MB，**交付形式确定为甲**。
- -Os 只省 0.47MB（6.6%），APM/Opus 的 CPU 代价要板上测，**暂保持 -O2**（R4）。
- 两种形式的动态依赖都只有 libstdc++、libm、libgcc_s、libc、ld-linux（ALSA/Pulse 运行时 dlopen）；要求的 GLIBC 最高 2.38，与板端一致。乙的 `.so` 只导出 `talk_probe_run` 一个符号。
- 体积分布（`scripts/size_breakdown.py`，-O2 甲）：pc 15%、BoringSSL 14%、video 6%、Opus 5.5%、APM 5.3%、p2p 5%、rtp_rtcp 5%、media 4.4%，其余都在 4% 以下；没有大块可裁的模块。

### 7.4 甲形式接入验证（R7）

- GCC 12 编译 WebRTC 头文件**无错误**，只有警告：`[[deprecated]]` 的 `AddReceiver`，以及 `rtc_base/network_constants.h` 里 `uint16_t` 常量溢出（上游代码问题，clang 下计算结果相同）。
- 应用侧编译参数见 `scripts/build_probe_gcc.sh`：`-std=c++20 -fno-rtti` + WebRTC 宏 + `-I src -I src/third_party/abseil-cpp -I src/third_party/libyuv/include`；链接 `--gc-sections`、`--start-group libwebrtc.a --end-group -lpthread -ldl -lm`。
- 继承 `PeerConnectionObserver` 等接口类、跨库传 `std::string` / `std::vector` / `scoped_refptr` 都正常，libstdc++ ABI 一致。

### 7.5 功能验证（主机，qemu-aarch64 用户态模拟）

探针 `TALK_PROBE_FAKE_AUDIO=1` 时用内置的 440Hz 正弦波假设备代替声卡（主机没有声卡）：

| 项 | 结果 |
|---|---|
| ICE + DTLS-SRTP | 两个 PeerConnection 都进入 connected |
| SDP 协商 | answer 含 `opus/48000/2`、`H264/90000`、`H265/90000` |
| 音频链路 | 甲、乙、clang 静态三个版本在 qemu 下都 PASS，接收端收到音频包，NetEQ 有输出 |

qemu 下播放端周期性出现满幅异常帧。排查过程：关掉 AGC2 后依旧存在；clang 静态版同样存在，排除 GCC/clang 混编的 ABI 问题。另编一个原生 x86_64 版本（`out/x64_check`）在实时速度下运行：8 秒收到 400 个包（正好 20ms 一包），播放 801 帧，**0 个削波样本**。结论是 qemu 只能跑到实时速度的约一半，NetEQ 持续欠载导致的假象，真实情况要在板子上确认。

### 7.6 下一步（P3，需要板子）

1. 拷 `build/probe_gcc/talk_probe_gcc` 到板子，先用 `TALK_PROBE_FAKE_AUDIO=1 ./talk_probe_gcc 10` 确认实时速度下 0 削波，再不带环境变量跑真实 ALSA/Pulse 设备。
2. 按 3.5 测回声消除、A53/A72 上的 CPU 占用，同时对比 -O2 和 -Os 两个版本，决定 R4。
3. P4 打包 SDK：头文件、`libwebrtc.a`、`webrtc_defines.cmake`（宏、include、`-fno-rtti`）、示例。
