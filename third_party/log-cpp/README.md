# log-cpp

薄封装的 C++ 日志库。引擎是 [spdlog](https://github.com/gabime/spdlog)，本库只负责统一初始化、按出口过滤级别、可选去重和 Qt 格式化。业务侧使用一层宏 + fmt `{}`，不要再叠第二层宏。

版本 **1.0.0**，C++14。默认编成**动态库**，让进程里的 A / B.dll / C.dll 共用一份单例。

---

## 设计逻辑

### 为什么封装

直接用 spdlog 可以工作，但工程里通常会反复写：建线程池、rolling 文件、控制台、级别、进程退出时 flush。再叠加「控制台和文件级别不同」「短时间重复错误合成一条」时，每个模块自己搞一份很容易变成多套互不相认的 logger。

本库约定：

1. **spdlog 做引擎**，格式语言保持 fmt，不退回 `printf`。
2. **一层宏**：`LOG_INFO` 直接进 `w_log_cpp::log`，没有 `TRACEI → ETRACEI → MTRACEI`。
3. **显式 init**：未调用 `init` 时丢弃日志并在 stderr 警告一次，避免第一次 `LOG_*` 偷偷建单例，导致后续 `init` / 线程池设置全部失效。
4. **公共头不暴露 sink 类型**：不在 `w_log_cpp.h` 里 `#include <spdlog/spdlog.h>`，也不接收 `spdlog::sinks_init_list`。头文件只为 fmt 引入 `spdlog/fmt/fmt.h`。
5. **级别按 sink 配置**，不写死「Debug 上控制台、Info 进文件」；那只是一种合法组合。
6. **Qt 可选**：需要 `QString` 时，业务侧自行 `#include "w_log_cpp/qt_formatter.h"`（并用本机 Qt）；库本体零 Qt 依赖，vcpkg 不拉 `qt5-base`。

### 数据路径

```text
LOG_INFO("open {}", path)
        │  编译期：LOGCPP_ACTIVE_LEVEL 可裁掉整句
        │  运行期：acquireLogger（短锁）→ should_log → memory_buffer format → 入队
        ▼
   进程内 Engine（一份，来自 log-cpp.dll）
        │  logger 门槛 = min(已启用各 sink 级别)
        ▼
  ┌─────────────┬──────────────────┐
  stdout sink   file sink          sqlite（可选）
  独立级别      独立级别            随 file 级别
                └ counting_dup_sink（可选，窗口内合并重复）
```

控制台和文件是两条 sink。logger 总门槛在 `init` / `setConsoleLevel` / `setFileLevel` 时自动取「已启用出口里更低的那个」。`InitConfig::level` 在 init 时忽略，不必再手动对齐。

### 一份日志还是多份

由**链接方式**决定，不是宏决定。

| 链接 | 进程里几套 Engine | 默认文件 |
|------|-------------------|----------|
| A / B / C 都动态链接同一个 `log-cpp.dll` | 1 | 默认一份 `app` logger |
| A / B / C 各自静态链接 log-cpp | 3 套互不相认 | 各自 init 才会各写各的 |
| 静态库 D 链进 A | 不算新套 | 走 A 的那份 |

推荐：log-cpp 用动态库；D 只静链进 A，不要再静链一份 log-cpp。需要第二份文件时用 `initNamed("sdk", ...)` + `LOG_ERR_TO("sdk", ...)`，仍是同一份 DLL。

`init` / `initNamed` 对同一名字第二次调用返回 `false`，不会静默覆盖。

### 重复日志

只包在**文件** sink 上。`file_dedup_ms > 0` 时：

- 首条立刻写文件
- 窗口内正文+级别相同则只计数
- 窗口结束、来了不同日志、或 `shutdown()` 时补写 `原正文 (xN)`

控制台默认不去重，避免调试时看不见刷屏。

---

## 构建

依赖：vcpkg `spdlog`。可选 `sqlitecpp`。`QString` 格式化用本机 Qt，不经 vcpkg。

```bash
cmake -B build -DLOGCPP_BUILD_TESTS=ON -DLOGCPP_BUILD_SHARED=ON
cmake --build build
# 高压吞吐：log_cpp_stress（多线程打日志，输出 logs/s）
# Qt 冒烟（本机 Qt）：-DLOGCPP_BUILD_QT_TEST=ON -DCMAKE_PREFIX_PATH=<Qt前缀>
```

常用开关：

| CMake | 默认 | 含义 |
|-------|------|------|
| `LOGCPP_BUILD_SHARED` | ON | 动态库，多模块共用单例 |
| `LOGCPP_BUILD_TESTS` | OFF | 编译 `test/main.cpp` 示例 |
| `LOGCPP_BUILD_QT_TEST` | OFF | 编译 `log_cpp_qt`（需本机 Qt Core） |
| `LOGCPP_SUPPORT_SQLITE` | OFF | `InitConfig::sqlite_path` |
| `LOGCPP_ACTIVE_LEVEL` | Debug（未定义 `NDEBUG`）/ Info（`NDEBUG`） | 编译期裁剪。Release 默认编掉 `LOG_DEBUG` / `LOG_SCOPE` |

级别都是可配置的，**没有写死**「Debug 只上控制台、Info 才进文件」；那只是案例 2 的一种组合。

消费工程为 **C++20** 时，公共头会用 `fmt::format_string` 做格式检查；动态格式串走 `fmt::runtime`。库本体仍为 C++14。MSVC 建议对消费端开启 `/std:c++20` 与 `/Zc:__cplusplus`（或依赖 `_MSVC_LANG`）。编译验证目标：`log_cpp_cxx20`。

### vcpkg 私有仓 / overlay

本仓库自带 overlay port：`ports/log-cpp/`。静态还是动态、要不要 SQLite，用 **feature**（以及 triplet）控制。Qt 不走 vcpkg feature。

```bash
vcpkg install log-cpp                --overlay-ports=<repo>/ports
vcpkg install log-cpp[shared]        --overlay-ports=<repo>/ports
vcpkg install log-cpp[sqlite,shared] --overlay-ports=<repo>/ports --triplet x64-windows-static
```

消费工程 manifest：

```json
{
  "dependencies": [
    {
      "name": "log-cpp",
      "features": ["shared"]
    }
  ]
}
```

| feature | 作用 |
|---------|------|
| （无） | 链接方式跟 triplet：`x64-windows` → 动态，`x64-windows-static` → 静态 |
| `shared` | 强制编成动态库（即使 triplet 是 static） |
| `sqlite` | `-DLOGCPP_SUPPORT_SQLITE=ON`，并拉 `sqlitecpp` |

`find_package(log-cpp)` 时的 `log-cppConfig.cmake` 会写死安装时的 SQLITE / SHARED，消费端不必再设这些 CMake 选项。

私有 git registry 把 `ports/log-cpp` 拷过去，把 `portfile.cmake` 里的 `SOURCE_PATH` 换成 `vcpkg_from_git` 即可。

---

## 使用案例

### 1. 最小接入

```cpp
#include "w_log_cpp.h"

int main() {
    w_log_cpp::InitConfig cfg;
    cfg.file_path = "./log/app.log";
    if (!w_log_cpp::init(cfg)) {
        return 1;
    }

    LOG_INFO("uptime {}", 1);
    LOG_WARN("cache miss {}", "user");
    LOG_ERR("open failed: {}", "EACCES");

    w_log_cpp::shutdown();
}
```

未 `init` 就打日志：消息丢弃，stderr 打一条警告。

### 2. 控制台与文件用不同级别

级别都可以设，下面只是其中一种组合（Debug 看屏幕、Info 才落盘）：

```cpp
w_log_cpp::InitConfig cfg;
cfg.file_path = "./log/app.log";
cfg.console_level = w_log_cpp::Level::Debug;  // 总门槛自动为 Debug
cfg.file_level = w_log_cpp::Level::Info;
w_log_cpp::init(cfg);

LOG_DEBUG("frame {}", 12);   // 仅控制台
LOG_INFO("export done");     // 控制台 + 文件
LOG_ERR("mux failed");       // 控制台 + 文件
```

运行期再改：

```cpp
w_log_cpp::setConsoleLevel(w_log_cpp::Level::Warn);  // 控制台只看 Warn+
w_log_cpp::setFileLevel(w_log_cpp::Level::Debug);    // 文件开始记 Debug
w_log_cpp::setLevel(w_log_cpp::Level::Info);         // 控制台和文件都重置为 Info
```

其它合法组合：两边都 Info；控制台 Off 只写文件；文件 Off 只打控制台。

### 3. 短时间重复错误合成次数

```cpp
w_log_cpp::InitConfig cfg;
cfg.file_path = "./log/app.log";
cfg.file_dedup_ms = 200;  // 0 表示关闭
w_log_cpp::init(cfg);

for (int i = 0; i < 100; ++i) {
    LOG_ERR("open failed: disk full");
}
w_log_cpp::shutdown();  // 必须 shutdown 或超出窗口，才会把 (xN) 刷出去
```

文件中类似：

```text
[2026-09-20 20:30:01.120] [E][foo.cpp:88,1234] open failed: disk full
[2026-09-20 20:30:01.320] [E][foo.cpp:88,1234] open failed: disk full (x100)
```

首条立刻落盘；`(xN)` 的 N **含首条**。控制台仍然每条都打，方便盯现场。

窗口结束、来了不同正文、或 `shutdown()` 才会把次数刷出去。`flush_on(Error)` 只刷文件，不会拆开窗口。

也可事后开关：`w_log_cpp::setFileDedup(200);`

### 4. 可选 Qt（QString）

库不链接 Qt。需要直接打 `QString` / `QByteArray` 时，在**已使用 Qt 的翻译单元**里多 include 一行 formatter，并用本机 Qt（`CMAKE_PREFIX_PATH`），不要经 vcpkg 装 `qt5-base`。

```cpp
#include "w_log_cpp.h"
#include "w_log_cpp/qt_formatter.h"  // 可选：fmt 特化
#include <QString>

QString id = QStringLiteral("draft-001");
LOG_INFO("open draft {}", id);
```

不 include `qt_formatter.h` 时不要传 `QString`；log-cpp 本身零 Qt 依赖。验证目标：`log_cpp_qt`（`-DLOGCPP_BUILD_QT_TEST=ON`）。

### 5. named logger：同一进程两份文件

仍是一份 `log-cpp.dll`，两个 logger 名。

```cpp
w_log_cpp::InitConfig app;
app.file_path = "./log/app.log";
w_log_cpp::init(app);              // 默认名字 "app"，LOG_INFO 走这里

w_log_cpp::InitConfig sdk;
sdk.name = "sdk";
sdk.file_path = "./log/sdk.log";
sdk.enable_console = false;
sdk.file_level = w_log_cpp::Level::Warn;
w_log_cpp::initNamed("sdk", sdk);

LOG_INFO("ui ready");              // app.log
LOG_ERR_TO("sdk", "bve {}", code); // sdk.log
```

工程 A 的 `main` 负责 `init`；动态库 B/C 和静态库 D 只打 `LOG_*`，不要各自再 init。

### 6. 线程池与 overflow（init 前）

```cpp
w_log_cpp::setThreadPoolSize(8192, 1);  // init 之后再调会返回 false
w_log_cpp::InitConfig cfg;
cfg.file_path = "./log/app.log";
cfg.overflow = w_log_cpp::Overflow::Block;  // 或 OverrunOldest
w_log_cpp::init(cfg);
```

默认队列 8192、1 个后台线程、队列满时阻塞（不丢日志）。

### 7. 函数进出耗时

```cpp
void exportVideo() {
    LOG_SCOPE();  // Debug 级别；Release 若 ACTIVE_LEVEL>=Info 则编译期去掉
    // ...
}
```

### 8. 多模块工程（A + B.dll + C.dll + D.lib）

```text
A.exe  ──动态── log-cpp.dll
B.dll  ──动态── log-cpp.dll     同一 Engine
C.dll  ──动态── log-cpp.dll
D.lib  静链进 A.exe              日志走 A 的 init
```

```cpp
// A/main.cpp
w_log_cpp::InitConfig cfg;
cfg.file_path = "./log/app.log";
cfg.console_level = w_log_cpp::Level::Debug;
cfg.file_level = w_log_cpp::Level::Info;
w_log_cpp::init(cfg);

// B.cpp / C.cpp / D.cpp 任意处
LOG_INFO("module {} ready", "B");
```

不要把 log-cpp 再静链进 B、C，否则会变成多份单例，`setFileLevel` 只改本 DLL。

---

## API 摘要

| API | 作用 |
|-----|------|
| `init` / `initNamed` | 创建默认或命名 logger |
| `shutdown` | flush 去重缓存、落盘、拆线程池 |
| `setLevel` | 控制台+文件一起改 |
| `setConsoleLevel` / `setFileLevel` | 按出口改 |
| `setFileDedup` | 文件去重窗口，毫秒，0 关闭 |
| `setThreadPoolSize` | 仅 init 前有效 |
| `setFlushOn` | 该级别及以上立即 flush |
| `LOG_TRACE` ~ `LOG_ERR` | 默认 logger |
| `LOG_*_TO(name, ...)` | 命名 logger |
| `LOG_SCOPE` | Debug 作用域 |

宏格式与 spdlog/fmt 相同：`LOG_INFO("a={} b={}", a, b);`

---

## 和旧版 0.0.2 的差异

| 旧 | 新 |
|----|----|
| `LOG_INFO("hello %s", x)` | `LOG_INFO("hello {}", x)` |
| `initLog({spdlog sinks})` | `init(InitConfig{...})` |
| `setLogLevel` + `setFileLogLevel` | `setLevel` / `setConsoleLevel` / `setFileLevel` |
| `shutdow()` | `shutdown()` |
| 第一次 LOG 隐式 init | 必须显式 `init` |
| 默认静态库、队列 10 | 默认动态库、队列 8192 |
