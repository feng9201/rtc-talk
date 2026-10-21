// File: core/base/io_runtime.h
// Brief: 核心控制线程（Asio io_context）的生命周期封装。
#ifndef TALK_BASE_IO_RUNTIME_H_
#define TALK_BASE_IO_RUNTIME_H_

#include <functional>
#include <thread>
#include <utility>

#include <asio.hpp>

namespace talk {

// 所有信令与状态变更都在该线程串行执行。
class IoRuntime {
 public:
  IoRuntime();
  ~IoRuntime();

  IoRuntime(const IoRuntime&) = delete;
  IoRuntime& operator=(const IoRuntime&) = delete;

  asio::io_context& io() { return io_; }

  template <class F>
  void Post(F&& task) {
    asio::post(io_, std::forward<F>(task));
  }

  // 幂等；返回后 io 线程不再执行任何回调。
  void Stop();

 private:
  using WorkGuard =
      asio::executor_work_guard<asio::io_context::executor_type>;

  asio::io_context io_;
  WorkGuard guard_;
  std::thread thread_;
  bool stopped_ = false;
};

}  // namespace talk

#endif  // TALK_BASE_IO_RUNTIME_H_
