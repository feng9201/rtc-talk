# 文件目录、模块和构建

下面是目标产品结构；当前真实实现以 [状态记录](status.md) 为准。
不能把目录树里的目标文件当成已交付代码。

```text
client/
  README.md
  CMakeLists.txt / CMakePresets.json
  cmake/                         # toolchain、SDK 导入、ARM64 triplet、严格链接
  config/                        # 有 schema 的生产/开发示例，无密码/token
  docs/
    architecture.md / directory.md / api.md / ui.md
    development-plan.md / status.md
    adr/                         # 重大取舍、兼容性与变更记录
  apps/
    rtc-cli/                     # 无 UI 的真实信令/媒体诊断客户端
    rtc-qt/                      # 依赖装配、单实例、生命周期
  include/rtc_client/             # 版本化 C ABI、配置和状态契约
  libs/
    domain/                      # 状态机、权限 gate、资源预算、typed IDs
    application/                 # 会话/房间/floor/call 控制器、ports、结果路由
    protocol/                    # talk.v1 JSON codec 与字段校验
    signaling/                   # WS/WSS、双向 RPC、heartbeat/reconnect
    media/                       # PC/track registry、音频、stats、能力探测
    video/                       # AU/帧租约、队列、解码/显示抽象
    platform/rk/                 # MPP decoder/factory/native buffer
    platform/wayland/            # sbdc Wayland 适配、布局/断线/在途租约
    logging/                     # log-cpp 适配、白名单、轮转、诊断快照
    client_api/                  # C ABI 参数检查/异常边界/所有权
    ui/                          # Store/Presenter/Widgets，不链接协议或 MPP
  vendor/                        # 经核实的最小受控源码，不含构建产物
    SOURCES.md / patches/        # 来源 revision/hash、许可、修改原因
  tests/
    unit/ integration/ contract/ # 假时钟/假 transport，不假装媒体集成已通过
    hardware/ interoperability/ # RK + Windows/SFU/TURN；可重复的采证脚本
    fixtures/                   # 不含真实身份与密钥
  scripts/                       # 构建/打包/诊断/安全部署；token 走 stdin
  deploy/                        # init 适配、启动/停止/回滚；不擅自安装
  .vscode/tasks.json
  build/                         # ignored，host/RK、vcpkg 缓存分别隔离
```

## 依赖和移植策略

1. Domain/video 基础设施可独立 host 编译，无 Qt、MPP、私有库依赖；先把并发和状态边界测清。
2. RK 实现仅通过 ports 注入；Windows 原客户端保持只读参考，不作本轮构建依赖。
3. vcpkg 固定 baseline、triplet、registry 与版本；log-cpp 固定 `1.0.0#4`，
   custom registry baseline `8334037c2977ae143cfec7b710366dcd7b4d166d`，
   built-in baseline `d4d39d71b3e6dd7536592c36ab2f7e84a8a64942`（沿用已验证组合）。
4. SDK 根只从环境/CMake 配置导入；机器绝对路径只在个人 preset，不硬编码源文件。
   WebRTC M146、GCC12.4、Qt5.15.11、BSP MPP/Wayland 的 ABI 矩阵写进构建 manifest。
5. sbdc 仅作为源码参考。提取前记录 commit + 工作区差异/hash，锁定最小依赖闭包、许可证和改动；
   若涉及私有接口/版本问题先请用户确认，不用无限试编译寻找组合。
6. 不复制 NVR demux/jitter/AI/多产品管理；视频队列和时钟按 WebRTC 需求实现。
7. SDK 固定制品可在 CI 缓存，但依赖源不可指向其他业务工程 build/；干净目录可重现构建。
8. 编译启用 warnings、hidden visibility、C ABI 导出表、`-z defs`、RPATH 检查；
   Debug/ASan/UBSan/TSan 分别用于可支持的平台，不能把 sanitizer 报告缺失当“零问题”。

## API 与平台隔离

公共 C ABI 不暴露 Qt/std::string/shared_ptr/WebRTC 对象。内部接口可用 RAII 和 typed values。
视频帧 lease 的 release 必须走显式契约；跨 DLL 的内存由分配方回收。
RK 同工具链也保留该边界，便于日后增加 Windows 后端；不为未来移植先制造多个无用进程。
