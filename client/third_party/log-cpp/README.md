# Encapsulated log SDK

只提交 C 头文件、平台共享库、校验清单和许可证说明；不提交私有 log-cpp 源码。
客户端正常构建不拉取、不编译私有库；fmt/spdlog/log-cpp 类型和 C++ 符号均不从 SDK 导出。

- `include/rtc_log_adapter.h`：自包含、版本化 C ABI。
- `lib/linux-aarch64/`：RK3576 `.so`；`lib/linux-x86_64/`：开发机测试 `.so`。
- `<platform>.json`：原库 revision、工具链、公开依赖版本、库与头文件 SHA256。
- `licenses/`：打包依赖的许可证。Windows `.dll` 必须另行构建验收，不能把 ELF 改名为 DLL。

共享库内静态纳入原实现，对应用只提供一份日志状态，避免多个业务模块各自静链产生多个单例。
应用只保留 `logging::Runtime` 的配置/生命周期与白名单事件适配；不能在实时音频回调中写日志。
最后一个 Runtime 在所有工作线程停止后调用 shutdown，排空日志队列。

重建时需要用户提供且已确认的本地源码（默认仓库根 `thirdPart/log-cpp`，已被 Git 忽略），
通过 `bash scripts/build-log-sdk.sh host` 或 `... rk3576` 生成。脚本不会下载 log-cpp。
不要只手工替换头文件或某一份 `.so`；头、库、清单必须一起更新并运行 ABI/日志测试。
